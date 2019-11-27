#include <vector>
#include <stdint.h>
#include <random>
#include <functional>

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

    // this reverses the bits but that doesn't matter for our purposes
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

struct TestEntry
{
    unsigned int value;
    std::function<float(const void* data_, uint64 length)> function;
};

static const TestEntry c_testBitCounts[] =
{
    {1, CalculateEntropyPerBit<1>},
    {4, CalculateEntropyPerBit<4>},
    {8, CalculateEntropyPerBit<8>},
    {11, CalculateEntropyPerBit<11>},
    {12, CalculateEntropyPerBit<12>},
    {16, CalculateEntropyPerBit<16>},
};

void DoTest(const char* label, const void* data, uint64 length)
{
    FILE* file = nullptr;
    fopen_s(&file, "out/entropy.csv", "a+t");
    fprintf(file, "\n\"%s\"", label);
    for (unsigned int index = 0; index < _countof(c_testBitCounts); ++index)
        fprintf(file, ",\"%f\"", c_testBitCounts[index].function(data, length));
    fclose(file);
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
    size_t actuallyRead = fread(data.data(), 1, data.size(), file);
    data.resize(actuallyRead); // strangely, i was seeing the ftell above give a different value (larger) than the actual amount of bytes readable.
    fclose(file);
    return true;
}

void DoFileTest(const char* fileName)
{
    std::vector<unsigned char> data;
    LoadFileIntoMemory(fileName, data);
    DoTest(fileName, data.data(), data.size());
}

void ClearCSV()
{
    FILE* file = nullptr;
    fopen_s(&file, "out/entropy.csv", "w+t");
    fprintf(file, "\"test\"");
    for (unsigned int index = 0; index < _countof(c_testBitCounts); ++index)
        fprintf(file, ",\"%u bits\"", c_testBitCounts[index].value);
    fclose(file);
}

static void BestCandidateN(std::vector<float>& values, size_t numValues, std::mt19937& rng, const size_t c_blueNoiseSampleMultiplier)
{
    // if they want less samples than there are, just truncate the sequence
    if (numValues <= values.size())
    {
        values.resize(numValues);
        return;
    }

    static std::uniform_real_distribution<float> dist(0, 1);

    // handle the special case of not having any values yet, so we don't check for it in the loops.
    if (values.size() == 0)
        values.push_back(dist(rng));

    // make a sorted list of existing samples
    std::vector<float> sortedValues;
    sortedValues = values;
    sortedValues.reserve(numValues);
    values.reserve(numValues);
    std::sort(sortedValues.begin(), sortedValues.end());

    // use whatever samples currently exist, and just add to them, since this is a progressive sequence
    for (size_t i = values.size(); i < numValues; ++i)
    {
        size_t numCandidates = values.size() * c_blueNoiseSampleMultiplier;
        float bestDistance = 0.0f;
        float bestCandidateValue = 0;
        size_t bestCandidateInsertLocation = 0;
        for (size_t candidate = 0; candidate < numCandidates; ++candidate)
        {
            float candidateValue = dist(rng);

            // binary search the sorted value list to find the values it's closest to.
            auto lowerBound = std::lower_bound(sortedValues.begin(), sortedValues.end(), candidateValue);
            size_t insertLocation = lowerBound - sortedValues.begin();

            // calculate the closest distance (torroidally) from this point to an existing sample by looking left and right.
            float distanceLeft = (insertLocation > 0)
                ? candidateValue - sortedValues[insertLocation - 1]
                : 1.0f + candidateValue - *sortedValues.rbegin();

            float distanceRight = (insertLocation < sortedValues.size())
                ? sortedValues[insertLocation] - candidateValue
                : distanceRight = 1.0f + sortedValues[0] - candidateValue;

            // whichever is closer left vs right is the closer point distance
            float minDist = std::min(distanceLeft, distanceRight);

            // keep the best candidate seen
            if (minDist > bestDistance)
            {
                bestDistance = minDist;
                bestCandidateValue = candidateValue;
                bestCandidateInsertLocation = insertLocation;
            }
        }

        // take the best candidate and also insert it into the sorted values
        sortedValues.insert(sortedValues.begin() + bestCandidateInsertLocation, bestCandidateValue);
        values.push_back(bestCandidateValue);
    }
}

int main(int argc, char** argv)
{
    ClearCSV();

    // file tests
    DoFileTest("Data/lastquestion.txt");
    DoFileTest("Data/lastquestion.enc");
    DoFileTest("Data/lastquestion.txt.zip");
    DoFileTest("Data/lastquestion.enc.zip");
    DoFileTest("Data/lastquestion.txt.zip.b64.txt");

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

    // blue noise
    {
        static std::mt19937 rng(GetRNGSeed());

        std::vector<float> randomNumbersFloat;
        BestCandidateN(randomNumbersFloat, 10000, rng, 1);

        std::vector<uint8_t> randomNumbers;
        randomNumbers.reserve(randomNumbersFloat.size());
        for (float f : randomNumbersFloat)
        {
            f = std::min(f * 256.0f, 255.0f);
            randomNumbers.push_back(uint8_t(f));
        }

        DoTest("Blue Noise", randomNumbers.data(), randomNumbers.size() * sizeof(randomNumbers[0]));
    }

    return 0;
}

/*

TODO:
* blue noise to compare to white. should be lower entropy density.  Could use code from "Noise Dims" and link to that blog post as to how you generated the blue noise. maybe do red noise too.

NOTES:

* in smaller white noise case, larger bit patterns can't POSSIBLY occur (not enough bits) so they are biased results. Same is true of all data in fact.
* the "byte based" streams show higher entropy for 11, 12 bits. that is kinda a lie though. explain why. the .zip doesnt show this and is more accurate / representative

* you would take minimum of entropies reported as the real entropy. 

* generated blue noise using techniques from here: https://blog.demofox.org/2019/07/30/dice-distributions-noise-colors/

* megathread: https://twitter.com/Atrix256/status/1189699372704878592?s=20
* another thread: https://twitter.com/Atrix256/status/1189933504294875136?s=20
* also this: https://johncarlosbaez.wordpress.com/2011/10/28/the-complexity-barrier/
* calculating entropy: http://webservices.itcs.umich.edu/mediawiki/lingwiki/index.php/Entropy
* correlation decreases entropy



Blue noise:

Reroll A, take A - B.  B is constant....
it's like triangular noise but you subtract out the entropy of one of the dice?

1 - B : 
2 - B :
3 - B :
4 - B :
5 - B :
6 - B : 



All possible values:

1 - 6 = -5
6 - 1 = 5

11 different values









*/