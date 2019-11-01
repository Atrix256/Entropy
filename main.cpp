#include <vector>
#include <stdint.h>

typedef uint64_t uint64;

inline bool GetNextBit(const char* data, uint64 length, uint64& bitOffset, uint64& byteOffset, uint64& value)
{
    if (byteOffset >= length)
        return false;

    uint64 mask = uint64(1) << bitOffset;

    value = value << 1;
    value = value | !!(data[byteOffset] & mask);

    bitOffset++;
    if (bitOffset == 8)
    {
        bitOffset = 0;
        byteOffset++;
    }

    return true;
}

template <uint64 NUMBITS>
bool GetNextValue(const char* data, uint64 length, uint64& bitOffset, uint64& byteOffset, uint64& value)
{
    value = 0;
    for (uint64 index = 0; index < NUMBITS; ++index)
    {
        if (!GetNextBit(data, length, bitOffset, byteOffset, value))
            return false;
    }
    return true;
}

template <uint64 NUMBITS>
float CalculateEntropyPerBit(const char* data, uint64 length)
{
    // calculate a histogram
    std::vector<uint64> histogram;
    {
        histogram.resize(1 << NUMBITS);
        uint64 bitOffset = 0;
        uint64 byteOffset = 0;
        uint64 value;
        while (GetNextValue<NUMBITS>(data, length, bitOffset, byteOffset, value))
            histogram[value]++;
    }

    // calculate entropy based on histogram
    // http://webservices.itcs.umich.edu/mediawiki/lingwiki/index.php/Entropy
    {
        uint64 totalCount = 0;
        for (uint64 count : histogram)
            totalCount += count;

        float entropy = 0.0f;
        for (uint64 count : histogram)
        {
            if (!count)
                continue;

            float probability = float(count) / float(totalCount);
            entropy -= probability * std::log2f(probability);
        }

        return entropy / float(NUMBITS);
    }
}

int main(int argc, char** argv)
{
    // TODO: need to normalize the entropy to like... entropy per bit. Did you do it right?
    // TODO: take min of entropy found?
    // TODO: do all the other tests you wanted to do!

    const char* string = "I like big butts and i cannot lie";
    printf("entropy<1> = %f\n", CalculateEntropyPerBit<1>(string, strlen(string)));
    printf("entropy<4> = %f\n", CalculateEntropyPerBit<4>(string, strlen(string)));
    printf("entropy<8> = %f\n", CalculateEntropyPerBit<8>(string, strlen(string)));
    printf("entropy<16> = %f\n", CalculateEntropyPerBit<16>(string, strlen(string)));
    return 0;
}