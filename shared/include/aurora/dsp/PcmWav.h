#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aurora
{
namespace dsp
{
class PcmWav
{
  public:
    static constexpr std::size_t headerBytes = 44;
    static constexpr uint32_t sampleRate = 48000;
    static constexpr uint16_t channelCount = 2;
    static constexpr uint16_t bitsPerSample = 16;
    static constexpr uint16_t blockAlign = channelCount * bitsPerSample / 8;

    struct Header
    {
        uint32_t frameCount = 0;
    };

    static bool ParseHeader(const uint8_t* bytes,
                            std::size_t byteCount,
                            Header& header)
    {
        if (bytes == nullptr || byteCount < headerBytes
            || !HasId(bytes, 0, "RIFF") || !HasId(bytes, 8, "WAVE")
            || !HasId(bytes, 12, "fmt ") || !HasId(bytes, 36, "data"))
            return false;

        const uint32_t dataBytes = Read32(bytes + 40);
        const uint32_t frames = dataBytes / blockAlign;
        if (dataBytes > 0xFFFFFFFFU - 36U
            || Read32(bytes + 4) != 36U + dataBytes
            || Read32(bytes + 16) != 16
            || Read16(bytes + 20) != 1
            || Read16(bytes + 22) != channelCount
            || Read32(bytes + 24) != sampleRate
            || Read32(bytes + 28) != sampleRate * blockAlign
            || Read16(bytes + 32) != blockAlign
            || Read16(bytes + 34) != bitsPerSample
            || dataBytes % blockAlign != 0
            || frames == 0)
            return false;

        header.frameCount = frames;
        return true;
    }

    static void WriteHeader(uint8_t* bytes, uint32_t frameCount)
    {
        const uint32_t dataBytes = frameCount * blockAlign;
        WriteId(bytes, 0, "RIFF");
        Write32(bytes + 4, 36U + dataBytes);
        WriteId(bytes, 8, "WAVE");
        WriteId(bytes, 12, "fmt ");
        Write32(bytes + 16, 16);
        Write16(bytes + 20, 1);
        Write16(bytes + 22, channelCount);
        Write32(bytes + 24, sampleRate);
        Write32(bytes + 28, sampleRate * blockAlign);
        Write16(bytes + 32, blockAlign);
        Write16(bytes + 34, bitsPerSample);
        WriteId(bytes, 36, "data");
        Write32(bytes + 40, dataBytes);
    }

    static int16_t ToPcm16(float sample)
    {
        if (sample <= -1.0f)
            return -32768;
        if (sample >= 1.0f)
            return 32767;
        const float scaled = sample * 32767.0f;
        return static_cast<int16_t>(scaled >= 0.0f
                                  ? std::floor(scaled + 0.5f)
                                  : std::ceil(scaled - 0.5f));
    }

    static float FromPcm16(int16_t sample)
    {
        return static_cast<float>(sample) / 32768.0f;
    }

  private:
    static bool HasId(const uint8_t* bytes,
                      std::size_t offset,
                      const char* id)
    {
        return bytes[offset] == static_cast<uint8_t>(id[0])
            && bytes[offset + 1] == static_cast<uint8_t>(id[1])
            && bytes[offset + 2] == static_cast<uint8_t>(id[2])
            && bytes[offset + 3] == static_cast<uint8_t>(id[3]);
    }

    static uint16_t Read16(const uint8_t* bytes)
    {
        return static_cast<uint16_t>(bytes[0])
             | static_cast<uint16_t>(bytes[1] << 8);
    }

    static uint32_t Read32(const uint8_t* bytes)
    {
        return static_cast<uint32_t>(bytes[0])
             | (static_cast<uint32_t>(bytes[1]) << 8)
             | (static_cast<uint32_t>(bytes[2]) << 16)
             | (static_cast<uint32_t>(bytes[3]) << 24);
    }

    static void WriteId(uint8_t* bytes, std::size_t offset, const char* id)
    {
        bytes[offset] = static_cast<uint8_t>(id[0]);
        bytes[offset + 1] = static_cast<uint8_t>(id[1]);
        bytes[offset + 2] = static_cast<uint8_t>(id[2]);
        bytes[offset + 3] = static_cast<uint8_t>(id[3]);
    }

    static void Write16(uint8_t* bytes, uint16_t value)
    {
        bytes[0] = static_cast<uint8_t>(value);
        bytes[1] = static_cast<uint8_t>(value >> 8);
    }

    static void Write32(uint8_t* bytes, uint32_t value)
    {
        bytes[0] = static_cast<uint8_t>(value);
        bytes[1] = static_cast<uint8_t>(value >> 8);
        bytes[2] = static_cast<uint8_t>(value >> 16);
        bytes[3] = static_cast<uint8_t>(value >> 24);
    }
};
}
}
