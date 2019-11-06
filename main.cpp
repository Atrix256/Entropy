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

template <uint64 NUMBITS, uint64 ORDER>
float CalculateEntropyPerBitMarkovChain(const void* data_, uint64 length)
{
    // calculate a histogram with state (a markov chain but with counts instead of probabilities)
    const char* data = (const char*)data_;
    std::vector<uint64> histogram;
    {
        // prime the currentValue so that it's ready to take it's first real sample
        uint64 currentValue = 0;
        uint64 value = 0;
        uint64 bitOffset = 0;
        uint64 byteOffset = 0;
        constexpr uint64 BITMASK = (uint64(1) << uint64(NUMBITS*(ORDER + 1))) - 1;
        for (uint64 index = 0; index < ORDER; ++index)
        {
            GetNextValue<NUMBITS>(data, length, bitOffset, byteOffset, value);
            currentValue = ((currentValue << NUMBITS) | value) & BITMASK;
        }

        // take samples until we run out of data
        histogram.resize(BITMASK + 1);
        while (GetNextValue<NUMBITS>(data, length, bitOffset, byteOffset, value))
        {
            currentValue = ((currentValue << NUMBITS) | value) & BITMASK;
            histogram[currentValue]++;
        }
    }

    // Calculate conditional entropy for each input group
    //
    // H(Y|X) = - Sum(p(x,y) * log(p(x,y)/p(x)))
    // The conditional entropy of y given x is equal to the negative sum of:
    //  * The joint probability of x and y multiplied by the log of:
    //    * The joint probability of x and y divided by
    //    * the probability of x
    //
    // x is the "previous values", y is the "current value"
    //
    // The conditional entropy of the current value given the previous values is equal to the negative sum of:
    //  * The joint probability of the old value and the new value multiplied by the log of:
    //    * The joint probability of the old value and the new value divided by
    //    * The probability of the old value
    //
    constexpr uint64 INPUT_COUNT = (uint64(1) << uint64(NUMBITS*(ORDER)));
    for (uint64 input = 0; input < INPUT_COUNT; ++input)
    {
        uint64 rangeStart = input << NUMBITS;
        uint64 rangeEnd = (input + 1) << NUMBITS;

        // get a total count for this input group so we can convert counts into probabilities
        uint64 totalCount = 0;
        for (uint64 currentValue = rangeStart; currentValue < rangeEnd; ++currentValue)
            totalCount += histogram[currentValue];

        // if none in this group, nothing to do
        if (totalCount == 0)
            continue;

        for (uint64 currentValue = rangeStart; currentValue < rangeEnd; ++currentValue)
        {
            if (!histogram[currentValue])
                continue;

            float probability = float(histogram[currentValue]) / float(totalCount);
            int ijkl = 0;
            // TODO: continue
            // TODO: maybe i should just calculate conditional probability instead and convert that to entropy? (can i?)
        }

        int ijkl = 0;
    }


    return 0.0f;
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
    {8, CalculateEntropyPerBitMarkovChain<8, 1>},
    {8, CalculateEntropyPerBitMarkovChain<8, 2>},
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
        fprintf(file, ",\"%u\"", c_testBitCounts[index].value);
    fclose(file);
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

    return 0;
}

/*

TODO:
? does an order 0 markov chain give you the same results? it should... try the function call?
 * we could remove the histogram code, but maybe worth keeping it around for readability?

* create markov chains and calculate conditional entropy, to try and show how correlated values show up there, but not in the histogram approach!

* maybe have the test entries let you have a label for the colum, so you can put info in about the order of the markov chain

* blue noise to compare to white. should be lower entropy density.  Could use code from "Noise Dims" and link to that blog post as to how you generated the blue noise. maybe do red noise too.
* non english language? random text? 6 vs 8 sided dice? images vs compressed images?

? do we need all those text files, or is 1 enough? probably 1 is enough.

NOTES:
* calculating entropy is actually not possible. It basically boils down to K complexity: the shortest program which can reproduce data.
 * So, it's a search to find a "view" of data which has the least entropy.
 * That "view" is instructions on how to compress the data!
 * we can make a histogram and calculate probabilities and then entropy (and we do in this post)
 * It doesn't take "frequency" into account though, so then comes the markov chain and conditional entropy calculations, which are still not complete, just more possible views of the data out of the infinite sea of possible views.
 * Compressing data can be a good way to see how much entropy there is in something. Counter example: encrypted data doesn't compress but encrypted data has the same amount of entropy as the plain text, it's just obfuscated.

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