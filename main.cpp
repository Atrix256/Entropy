#include <vector>
#include <stdint.h>
#include <random>

typedef uint64_t uint64;

#define DETERMINISTIC() true  // if true, will use the seed below for everything, else will randomly generate a seed.
#define DETERMINISTIC_SEED() unsigned(783104853), unsigned(4213684301), unsigned(3526061164), unsigned(614346169), unsigned(478811579), unsigned(2044310268), unsigned(3671768129), unsigned(206439072)

inline std::seed_seq& GetRNGSeed()
{
#if DETERMINISTIC()
    static std::seed_seq fullSeed{ DETERMINISTIC_SEED() };
#else
    static std::random_device rd;
    static std::seed_seq fullSeed{ rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd() };
#endif
    return fullSeed;
}

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
float CalculateEntropyPerBit(const void* data_, uint64 length)
{
    // calculate a histogram
    const char* data = (const char*)data_;
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


void DoTest(const char* label, const void* data, uint64 length)
{
    printf("\n%s (%u bits):\n", label, (unsigned int)length*8);
    printf("entropy<1> = %f\n", CalculateEntropyPerBit<1>(data, length));
    printf("entropy<4> = %f\n", CalculateEntropyPerBit<4>(data, length));
    printf("entropy<8> = %f\n", CalculateEntropyPerBit<8>(data, length));
    printf("entropy<11> = %f\n", CalculateEntropyPerBit<11>(data, length));
    printf("entropy<12> = %f\n", CalculateEntropyPerBit<12>(data, length));
    printf("entropy<16> = %f\n", CalculateEntropyPerBit<16>(data, length));
}

bool LoadFileIntoMemory(const char* fileName, std::vector<unsigned char>& data)
{
    FILE* file = nullptr;
    fopen_s(&file, fileName, "rt");
    if (!file)
        return false;
    fseek(file, 0, SEEK_END);
    data.resize(ftell(file));
    fseek(file, 0, SEEK_SET);
    fread(data.data(), 1, data.size(), file);
    fclose(file);
    return true;
}

void DoFileTest(const char* fileName)
{
    std::vector<unsigned char> data;
    LoadFileIntoMemory(fileName, data);
    DoTest(fileName, data.data(), data.size());
}

int main(int argc, char** argv)
{
    // file tests
    DoFileTest("Data/lastquestion.txt");
    DoFileTest("Data/lastquestion.enc");
    DoFileTest("Data/lastquestion.txt.zip");
    DoFileTest("Data/lastquestion.enc.zip");
    DoFileTest("Data/lastquestion.txt.zip.b64.txt");

    DoFileTest("Data/projbluenoise.txt");
    DoFileTest("Data/psychreport.txt");
    DoFileTest("Data/telltale.txt");

    // small white noise
    {
        static std::mt19937 rng(GetRNGSeed());
        static std::uniform_int_distribution<uint64> dist;

        std::vector<uint64> randomNumbers(1);
        for (uint64& v : randomNumbers)
            v = dist(rng);

        DoTest("Small White Noise", randomNumbers.data(), randomNumbers.size() * sizeof(randomNumbers[0]));
    }

    // white noise
    {
        static std::mt19937 rng(GetRNGSeed());
        static std::uniform_int_distribution<uint64> dist;

        std::vector<uint64> randomNumbers(100000);
        for (uint64& v : randomNumbers)
            v = dist(rng);

        DoTest("White Noise", randomNumbers.data(), randomNumbers.size() * sizeof(randomNumbers[0]));
    }

    system("pause");
    return 0;
}

/*

TODO:
* blue noise to compare to white. should be lower entropy density.  Could use code from "Noise Dims" and link to that blog post as to how you generated the blue noise. maybe do red noise too.
* non english language? random text? 6 vs 8 sided dice? images vs compressed images

* maybe show graph of entropy per bit count for each test? make a csv i guess
? do we need all those text files, or is 1 enough? probably 1 is enough.

NOTES:
* in smaller white noise case, larger bit patterns can't POSSIBLY occur (not enough bits) so they are biased results. Same is true of all data in fact.
* you could find that study about there being 2 bits of info per letter. how does yours compare, and why?
* the "byte based" streams show higher entropy for 11, 12 bits. that is kinda a lie though. explain why. the .zip doesnt show this and is more accurate / representative
* notice how the encrypted file didn't compress any!
* zipped and encrypted data increases entropy density. encryption does moreso.

* you would take minimum of entropies reported as the real entropy. 

* generated blue noise using techniques from here: https://blog.demofox.org/2019/07/30/dice-distributions-noise-colors/

* megathread: https://twitter.com/Atrix256/status/1189699372704878592?s=20
* another thread: https://twitter.com/Atrix256/status/1189933504294875136?s=20
* also this: https://johncarlosbaez.wordpress.com/2011/10/28/the-complexity-barrier/
* calculating entropy: http://webservices.itcs.umich.edu/mediawiki/lingwiki/index.php/Entropy
* correlation decreases entropy

* encryption command line:
* openssl enc -aes-256-cbc -salt -in lastquestion.txt -out lastquestion.enc -pass pass:moreentropyplease


*/