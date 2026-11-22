#pragma once

#include "aurora/dsp/PcmWav.h"
#include "aurora/dsp/StereoLooper.h"
#include "fatfs.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

class LoopWavStore
{
  public:
    using ProgressCallback = void (*)();

    enum class Result
    {
        Loaded,
        NoFile,
        Ok,
        InvalidFile,
        IoError
    };

    const char* LastErrorOperation() const { return lastErrorOperation_; }
    FRESULT LastFatFsResult() const { return lastFatFsResult_; }

    Result Load(const char* path,
                float* left,
                float* right,
                std::size_t capacity,
                std::size_t& loadedFrames,
                ProgressCallback progress = nullptr)
    {
        ClearError();
        loadedFrames = 0;
        FIL file;
        const FRESULT openResult = f_open(&file, path, FA_READ);
        if (openResult == FR_NO_FILE)
        {
            SetError("load_file_missing", openResult);
            return Result::NoFile;
        }
        if (openResult != FR_OK)
        {
            SetError("load_open", openResult);
            return Result::IoError;
        }

        uint8_t headerBytes[aurora::dsp::PcmWav::headerBytes]{};
        aurora::dsp::PcmWav::Header header;
        const Result headerResult = ReadHeader(file, headerBytes, header);
        if (headerResult != Result::Loaded)
        {
            f_close(&file);
            return headerResult;
        }
        if (header.frameCount > capacity)
        {
            SetError("wav_too_large", FR_OK);
            f_close(&file);
            return Result::InvalidFile;
        }

        const std::size_t frames = header.frameCount;
        const FRESULT seekResult = f_lseek(
            &file, aurora::dsp::PcmWav::headerBytes);
        if (seekResult != FR_OK)
        {
            SetError("load_seek_audio", seekResult);
            f_close(&file);
            return Result::IoError;
        }

        int16_t samples[512]{};
        std::size_t remaining = frames;
        std::size_t offset = 0;
        while (remaining > 0)
        {
            const std::size_t chunkFrames = std::min<std::size_t>(remaining, 256);
            const UINT bytes = static_cast<UINT>(
                chunkFrames * aurora::dsp::PcmWav::blockAlign);
            UINT bytesRead = 0;
            const FRESULT readResult = f_read(&file, samples, bytes, &bytesRead);
            if (readResult != FR_OK || bytesRead != bytes)
            {
                SetError("load_read_audio",
                         readResult != FR_OK ? readResult : FR_DISK_ERR);
                f_close(&file);
                return Result::IoError;
            }
            for (std::size_t frame = 0; frame < chunkFrames; ++frame)
            {
                left[offset + frame] = aurora::dsp::PcmWav::FromPcm16(
                    samples[frame * 2]);
                right[offset + frame] = aurora::dsp::PcmWav::FromPcm16(
                    samples[frame * 2 + 1]);
            }
            if (progress != nullptr)
                progress();
            offset += chunkFrames;
            remaining -= chunkFrames;
        }

        const FRESULT closeResult = f_close(&file);
        if (closeResult != FR_OK)
        {
            SetError("load_close", closeResult);
            return Result::IoError;
        }
        loadedFrames = frames;
        return Result::Loaded;
    }

    Result Save(const char* path,
                const float* left,
                const float* right,
                std::size_t capacity,
                std::size_t desiredFrames,
                const aurora::dsp::StereoLooper::DirtyRange& leftDirty,
                const aurora::dsp::StereoLooper::DirtyRange& rightDirty,
                ProgressCallback progress = nullptr)
    {
        ClearError();
        if (left == nullptr || right == nullptr || capacity == 0
            || desiredFrames == 0 || desiredFrames > capacity
            || (!leftDirty.dirty && !rightDirty.dirty))
        {
            SetError("save_invalid_arguments", FR_OK);
            return Result::InvalidFile;
        }

        FIL file;
        FRESULT result = f_open(&file, path, FA_READ | FA_WRITE);
        bool created = false;
        uint32_t fileFrames = 0;
        if (result == FR_NO_FILE)
        {
            result = f_open(&file, path, FA_WRITE | FA_CREATE_NEW);
            if (result != FR_OK)
            {
                SetError("save_create_open", result);
                return Result::IoError;
            }
            created = true;
            fileFrames = static_cast<uint32_t>(desiredFrames);
            const Result createResult = CreateFile(file, fileFrames, progress);
            if (createResult != Result::Ok)
            {
                f_close(&file);
                f_unlink(path);
                return createResult;
            }
        }
        else if (result != FR_OK)
        {
            SetError("save_open_existing", result);
            return Result::IoError;
        }
        else
        {
            uint8_t headerBytes[aurora::dsp::PcmWav::headerBytes]{};
            aurora::dsp::PcmWav::Header header;
            const Result headerResult = ReadHeader(file, headerBytes, header);
            if (headerResult != Result::Loaded)
            {
                f_close(&file);
                return headerResult;
            }
            fileFrames = header.frameCount;
        }

        const uint32_t targetFrames = static_cast<uint32_t>(desiredFrames);
        if (targetFrames > capacity
            || targetFrames > (0xFFFFFFFFU - 36U)
                                  / aurora::dsp::PcmWav::blockAlign)
        {
            SetError("save_invalid_length", FR_OK);
            f_close(&file);
            return Result::InvalidFile;
        }

        std::size_t writeBegin = capacity;
        std::size_t writeEnd = 0;
        AddRange(leftDirty, capacity, targetFrames, writeBegin, writeEnd);
        AddRange(rightDirty, capacity, targetFrames, writeBegin, writeEnd);
        if (!created && targetFrames > fileFrames)
        {
            writeBegin = std::min<std::size_t>(writeBegin, fileFrames);
            writeEnd = targetFrames;
        }
        if (writeBegin >= writeEnd)
        {
            SetError("save_empty_range", FR_OK);
            f_close(&file);
            return Result::InvalidFile;
        }

        const FRESULT audioSeekResult = f_lseek(
            &file,
            static_cast<FSIZE_t>(aurora::dsp::PcmWav::headerBytes)
                + writeBegin * aurora::dsp::PcmWav::blockAlign);
        if (audioSeekResult != FR_OK)
        {
            SetError("save_seek_audio", audioSeekResult);
            f_close(&file);
            return Result::IoError;
        }
        if (!WriteFrames(file, left, right, writeBegin, writeEnd, progress))
        {
            f_close(&file);
            return Result::IoError;
        }

        if (targetFrames != fileFrames)
        {
            uint8_t headerBytes[aurora::dsp::PcmWav::headerBytes]{};
            aurora::dsp::PcmWav::WriteHeader(headerBytes, targetFrames);
            const FRESULT headerSeekResult = f_lseek(&file, 0);
            if (headerSeekResult != FR_OK)
            {
                SetError("save_seek_header", headerSeekResult);
                f_close(&file);
                return Result::IoError;
            }
            if (!WriteBytes(file,
                            headerBytes,
                            sizeof(headerBytes),
                            "save_write_header"))
            {
                f_close(&file);
                return Result::IoError;
            }
            const FSIZE_t finalSize = aurora::dsp::PcmWav::headerBytes
                                    + static_cast<FSIZE_t>(targetFrames)
                                          * aurora::dsp::PcmWav::blockAlign;
            const FRESULT truncateSeekResult = f_lseek(&file, finalSize);
            if (truncateSeekResult != FR_OK)
            {
                SetError("save_seek_truncate", truncateSeekResult);
                f_close(&file);
                return Result::IoError;
            }
            const FRESULT truncateResult = f_truncate(&file);
            if (truncateResult != FR_OK)
            {
                SetError("save_truncate", truncateResult);
                f_close(&file);
                return Result::IoError;
            }
        }

        result = f_sync(&file);
        const FRESULT closeResult = f_close(&file);
        if (result != FR_OK)
            SetError("save_sync", result);
        else if (closeResult != FR_OK)
            SetError("save_close", closeResult);
        return result == FR_OK && closeResult == FR_OK
             ? Result::Ok
             : Result::IoError;
    }

  private:
    const char* lastErrorOperation_ = "none";
    FRESULT lastFatFsResult_ = FR_OK;

    void ClearError()
    {
        lastErrorOperation_ = "none";
        lastFatFsResult_ = FR_OK;
    }

    void SetError(const char* operation, FRESULT result)
    {
        lastErrorOperation_ = operation;
        lastFatFsResult_ = result;
    }

    Result ReadHeader(FIL& file,
                      uint8_t* bytes,
                      aurora::dsp::PcmWav::Header& header)
    {
        UINT bytesRead = 0;
        const FRESULT seekResult = f_lseek(&file, 0);
        if (seekResult != FR_OK)
        {
            SetError("wav_seek_header", seekResult);
            return Result::IoError;
        }
        const FRESULT readResult = f_read(&file,
                                          bytes,
                                          aurora::dsp::PcmWav::headerBytes,
                                          &bytesRead);
        if (readResult != FR_OK || bytesRead != aurora::dsp::PcmWav::headerBytes)
        {
            SetError("wav_read_header",
                     readResult != FR_OK ? readResult : FR_DISK_ERR);
            return Result::IoError;
        }

        if (!aurora::dsp::PcmWav::ParseHeader(
                bytes,
                aurora::dsp::PcmWav::headerBytes,
                header))
        {
            SetError("wav_invalid_header", FR_OK);
            return Result::InvalidFile;
        }

        const FSIZE_t expectedSize = aurora::dsp::PcmWav::headerBytes
                                   + static_cast<FSIZE_t>(header.frameCount)
                                         * aurora::dsp::PcmWav::blockAlign;
        if (f_size(&file) != expectedSize)
        {
            SetError("wav_invalid_size", FR_OK);
            return Result::InvalidFile;
        }
        return Result::Loaded;
    }

    Result CreateFile(FIL& file,
                      uint32_t frames,
                      ProgressCallback progress)
    {
        uint8_t headerBytes[aurora::dsp::PcmWav::headerBytes]{};
        aurora::dsp::PcmWav::WriteHeader(headerBytes, frames);
        if (!WriteBytes(file,
                        headerBytes,
                        sizeof(headerBytes),
                        "save_create_header"))
            return Result::IoError;

        const uint8_t silence[512]{};
        std::size_t bytesRemaining = static_cast<std::size_t>(frames)
                                   * aurora::dsp::PcmWav::blockAlign;
        while (bytesRemaining > 0)
        {
            const UINT chunkBytes = static_cast<UINT>(
                std::min<std::size_t>(bytesRemaining, sizeof(silence)));
            if (!WriteBytes(file, silence, chunkBytes, "save_create_silence"))
                return Result::IoError;
            if (progress != nullptr)
                progress();
            bytesRemaining -= chunkBytes;
        }
        return Result::Ok;
    }

    bool WriteFrames(FIL& file,
                     const float* left,
                     const float* right,
                     std::size_t begin,
                     std::size_t end,
                     ProgressCallback progress)
    {
        int16_t samples[512]{};
        while (begin < end)
        {
            const std::size_t chunkFrames = std::min<std::size_t>(256, end - begin);
            for (std::size_t frame = 0; frame < chunkFrames; ++frame)
            {
                samples[frame * 2] =
                    aurora::dsp::PcmWav::ToPcm16(left[begin + frame]);
                samples[frame * 2 + 1] =
                    aurora::dsp::PcmWav::ToPcm16(right[begin + frame]);
            }
            const UINT bytes = static_cast<UINT>(
                chunkFrames * aurora::dsp::PcmWav::blockAlign);
            if (!WriteBytes(file, samples, bytes, "save_write_audio"))
                return false;
            if (progress != nullptr)
                progress();
            begin += chunkFrames;
        }
        return true;
    }

    bool WriteBytes(FIL& file,
                    const void* bytes,
                    UINT byteCount,
                    const char* operation)
    {
        UINT bytesWritten = 0;
        const FRESULT result = f_write(&file, bytes, byteCount, &bytesWritten);
        if (result != FR_OK || bytesWritten != byteCount)
        {
            SetError(operation,
                     result != FR_OK ? result : FR_DISK_ERR);
            return false;
        }
        return true;
    }

    static void AddRange(const aurora::dsp::StereoLooper::DirtyRange& range,
                         std::size_t capacity,
                         std::size_t targetFrames,
                         std::size_t& begin,
                         std::size_t& end)
    {
        if (!range.dirty || range.begin >= range.end
            || range.begin >= capacity)
            return;
        const std::size_t boundedEnd = std::min(range.end,
                                                std::min(capacity, targetFrames));
        if (range.begin >= boundedEnd)
            return;
        begin = std::min(begin, range.begin);
        end = std::max(end, boundedEnd);
    }
};
