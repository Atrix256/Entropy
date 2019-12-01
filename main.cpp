#include <vector>
#include <stdint.h>
#include <random>
#include <functional>
#include <chrono>

typedef uint64_t uint64;

#define DETERMINISTIC() true  // if true, will use the seed below for everything, else will randomly generate a seed.
#define DETERMINISTIC_SEED() unsigned(783104853), unsigned(4213684301), unsigned(3526061164), unsigned(614346169), unsigned(478811579), unsigned(2044310268), unsigned(3671768129), unsigned(206439072)

struct ScopedTimer
{
    ScopedTimer(const char* label)
    {
        printf("%s: ", label);
        m_start = std::chrono::high_resolution_clock::now();
    }

    ~ScopedTimer()
    {
        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(end - m_start);
        printf("%f ms\n", time_span.count() * 1000.0f);
    }

    std::chrono::high_resolution_clock::time_point m_start;
};

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

template <uint64 NUMBITS, uint64 ADVANCEMENTBITS>
bool GetNextValue(const char* data, uint64 length, uint64& bitOffset, uint64& byteOffset, uint64& value)
{
    uint64 startingBitOffset = bitOffset;
    uint64 startingByteOffset = byteOffset;

    value = 0;
    for (uint64 index = 0; index < NUMBITS; ++index)
    {
        if (!GetNextBit(data, length, bitOffset, byteOffset, value))
            return false;
    }

    // the caller may not want us to move forward the full amount
    bitOffset = startingBitOffset + ADVANCEMENTBITS;
    byteOffset = startingByteOffset + bitOffset / 8;
    bitOffset = bitOffset % 8;
    return true;
}

template <uint64 NUMBITS, uint64 ADVANCEMENTBITS>
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
        while (GetNextValue<NUMBITS, ADVANCEMENTBITS>(data, length, bitOffset, byteOffset, value))
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
    std::function<float(const void* data_, uint64 length)> function;
    const char* label;
};

static const TestEntry c_testBitCounts[] =
{
    {CalculateEntropyPerBit<1, 1>, "1 bit"},
    {CalculateEntropyPerBit<4, 4>, "4 bits"},
    {CalculateEntropyPerBit<8, 8>, "8 bits"},
    {CalculateEntropyPerBit<11, 11>, "11 bits"},
    {CalculateEntropyPerBit<12, 12>, "12 bits"},
    {CalculateEntropyPerBit<16, 16>, "16 bits"},
    {CalculateEntropyPerBit<16, 8>, "8 bits order 1"},
};

void DoTest(const char* label, const void* data, uint64 length)
{
    FILE* file = nullptr;
    fopen_s(&file, "out/entropy.csv", "a+t");
    fprintf(file, "\n\"%s\",\"%zu\"", label, length);
    for (unsigned int index = 0; index < _countof(c_testBitCounts); ++index)
        fprintf(file, ",\"%f\"", c_testBitCounts[index].function(data, length));
    fclose(file);
}

bool LoadFileIntoMemory(const char* fileName, std::vector<unsigned char>& data)
{
    FILE* file = nullptr;
    fopen_s(&file, fileName, "rb");
    if (!file)
        return false;
    fseek(file, 0, SEEK_END);
    data.resize(ftell(file));
    fseek(file, 0, SEEK_SET);
    size_t actuallyRead = fread(data.data(), 1, data.size(), file);
    data.resize(actuallyRead);
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
    fprintf(file, "\"test\",\"bytes\"");
    for (unsigned int index = 0; index < _countof(c_testBitCounts); ++index)
        fprintf(file, ",\"%s\"", c_testBitCounts[index].label);
    fclose(file);
}

template <typename T>
T Clamp(const T& x, const T& min, const T& max)
{
    if (x <= min)
        return min;
    else if (x >= max)
        return max;
    else
        return x;
}

inline size_t GetLowerBound(const std::vector<float>& values, const float& searchValue)
{
#if 0
    // since blue noise is roughly evenly distributed, i figure i'd try a linear interpolation
    // guess for lower bound, then try a linear search. it's slower for some reason though.

    // 855745.594400 ms for 100k

    // returns the first index in values that is >= searchValue
    size_t valueCount = values.size();
    float guessIndexF = std::max(searchValue * float(valueCount) + 0.5f, 0.0f);
    size_t guessIndex = std::min(size_t(guessIndexF), valueCount - 1);

    // if our guess is too large, we need to scan backwards
    if (values[guessIndex] >= searchValue)
    {
        while (guessIndex > 0 && values[guessIndex - 1] >= searchValue)
            guessIndex--;
    }
    // else our guess was too low, so we need to scan forwards
    else
    {
        while (guessIndex < valueCount && values[guessIndex] < searchValue)
            guessIndex++;
    }
    return guessIndex;

#else
    //  619405.554500 ms for 100k
    return std::lower_bound(values.begin(), values.end(), searchValue) - values.begin();
#endif
}

static void BestCandidateN(std::vector<float>& values, size_t numValues, std::mt19937& rng, const size_t c_blueNoiseSampleMultiplier)
{
    ScopedTimer timer("BestCandidate N");

    printf("Generating %zu blue noise floats:\n", numValues);

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
    int lastPercent = -1;
    for (size_t i = values.size(); i < numValues; ++i)
    {
        int percent = int(100.0f * float(i) / float(numValues));
        if (percent != lastPercent)
        {
            lastPercent = percent;
            printf("\r%i%%", lastPercent);
        }

        size_t numCandidates = values.size() * c_blueNoiseSampleMultiplier;
        float bestDistance = 0.0f;
        float bestCandidateValue = 0;
        size_t bestCandidateInsertLocation = 0;
        for (size_t candidate = 0; candidate < numCandidates; ++candidate)
        {
            float candidateValue = dist(rng);

            size_t insertLocation = GetLowerBound(sortedValues, candidateValue);

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
    printf("\r100%%\n");
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

        std::vector<uint64> randomNumbers(12500); // so it's 100k 8 bit values and can be compared apples to apples with the 100k 8 bit blue noise values
        for (uint64& v : randomNumbers)
            v = dist(rng);

        DoTest("White Noise", randomNumbers.data(), randomNumbers.size() * sizeof(randomNumbers[0]));

        FILE* file = nullptr;
        fopen_s(&file, "out/white_noise_u64.txt", "w+t");
        for (uint64 u : randomNumbers)
            fprintf(file, "%zu\n", u);
        fclose(file);
    }

    // blue noise
    {
        static std::mt19937 rng(GetRNGSeed());

        std::vector<float> randomNumbersFloat;
        BestCandidateN(randomNumbersFloat, 100000, rng, 1);

        std::vector<uint8_t> randomNumbers;
        randomNumbers.reserve(randomNumbersFloat.size());
        for (float f : randomNumbersFloat)
        {
            f = std::min(f * 256.0f, 255.0f);
            randomNumbers.push_back(uint8_t(f));
        }

        DoTest("Blue Noise", randomNumbers.data(), randomNumbers.size() * sizeof(randomNumbers[0]));

        FILE* file = nullptr;
        fopen_s(&file, "out/blue_noise_f32.txt","w+t");
        for (float f : randomNumbersFloat)
            fprintf(file, "%f\n", f);
        fclose(file);

        fopen_s(&file, "out/blue_noise_u8.txt", "w+t");
        for (uint8_t u : randomNumbers)
            fprintf(file, "%u\n", u);
        fclose(file);
    }

    system("pause");

    return 0;
}
