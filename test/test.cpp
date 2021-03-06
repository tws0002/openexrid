#include <vector>
#include <iostream>
#include <string>
#include <cstdio>
#include <stdexcept>
#include "../openexrid/Builder.h"
#include "../openexrid/Mask.h"
#include "../openexrid/Query.h"

using namespace std;
using namespace openexrid;

// Number max of samples per pixel during the test
const int SamplesMax = 4;

// Number of different names
const int NameN = 100;

// Image resolution
const int Width = 192;
const int Height = 108;

// Number of errors during the test
int Errors = 0;

// The temporary file
const char *filename = "temp.exr";

std::string to_string (int d)
{
	char buffer[256];
	std::sprintf (buffer, "%d", d);
	return std::string (buffer);
}

class TestMatch
{
public:
	std::vector<std::string>	&names;
	int				i;
	TestMatch (std::vector<std::string> &_names, int _i) : names (_names), i (_i)
	{}
	bool operator () (const char *name) const
	{
		return names[i] == name;
	}
};

// The name id per pixels
struct entry
{
	int Id;
	float Z;
	float Values[4];
	float Weight;
	bool operator<(const entry &o) const
	{
		return Z < o.Z;
	}
};

int main(int argc, char **argv)
{
	try
	{
		// Generate some names
		vector<string> names;
		for (int i = 0; i < NameN; ++i)
			names.push_back (::to_string (rand()));

		vector<vector<entry> >	pixelToNames (Width*Height);

		// Generate a random image
		for (vector<vector<entry> >::iterator itp = pixelToNames.begin (); itp != pixelToNames.end (); ++itp)
		{
			const size_t x = (itp - pixelToNames.begin ())%Width;
			const size_t y = (itp - pixelToNames.begin ())/Width;

			// Compute an empty rectangle around to test the dataWindow
			if (x == 0 || x == Width-1 || y == 0 || y == Height-1)
				continue;

			const int samplesN = rand ()%(SamplesMax+1);
			const float weight = 1.f/(float)samplesN;
			const int baseId = rand();
			for (int s = 0; s < samplesN; ++s)
			{
				entry	e;
				e.Id = (baseId+s*10)%NameN;
				e.Z = (float)((baseId+s*10)%NameN);
				e.Values[0] = (float)rand()/(float)RAND_MAX;
				e.Values[1] = (float)rand()/(float)RAND_MAX;
				e.Values[2] = (float)rand()/(float)RAND_MAX;
				e.Values[3] = (float)rand()/(float)RAND_MAX;
				e.Weight = weight;
				itp->push_back (e);
			}

			// Sort by Z
			sort (itp->begin(), itp->end ());
		}

		cout << "Fill a mask id map" << endl;
		std::vector<std::string> slices;
		slices.push_back ("R");
		slices.push_back ("G");
		slices.push_back ("B");
		slices.push_back ("A");
		Builder builder (Width, Height, slices);
		const int TestA = 3;

		// Fill the builder
		for (int y = 0; y < Height; ++y)
		for (int x = 0; x < Width; ++x)
		{
			const vector<entry> &pixel = pixelToNames[x+y*Width];
			for (size_t s = 0; s < pixel.size (); ++s)
				// Let's use the id as Z value
				builder.addCoverage (x, y, pixel[s].Id, pixel[s].Z, pixel[s].Weight, pixel[s].Values);
		}

		std::vector<float> pixelWeights (Width*Height, 0.f);
		{
			// Concat the names
			std::string _names;
			for (vector<string>::const_iterator ite = names.begin(); ite != names.end(); ++ite)
			{
				_names += *ite;
				_names += '\0';
			}

			// Build the final weight array
			for (int i =0; i < Width*Height; ++i)
			{
				const vector<entry> &pixel = pixelToNames[i];
				for (size_t s = 0; s < pixel.size (); ++s)
					pixelWeights[i] += pixel[s].Weight;
			}

			cout << "Finish the mask" << endl;
			builder.finish (pixelWeights);

			cout << "Write the mask" << endl;
			builder.write (filename, _names.c_str (), (int)_names.size(), true);
		}

		// Accumulate the pixels values
		for (int y=0; y<Height; ++y)
		for (int x=0; x<Width; ++x)
		{
			vector<entry> &pixel = pixelToNames[x+y*Width];
			if (!pixel.empty())
			{
				// First normalize the pixel using the weights
				const float weightSum = pixelWeights[x+y*Width];
				for (int v = 0; v < 4; ++v)
				{
					for (size_t s = 0; s < pixel.size (); ++s)
					{
						if (weightSum != 0)
							pixel[s].Values[v] = pixel[s].Values[v]*pixel[s].Weight/weightSum;
					}
				}

				// The accumulates the fragments in a deep fashion way
				for (int v = 0; v < 4; ++v)
				{
					float acc = 0.f;
					for (size_t s = 0; s < pixel.size (); ++s)
						pixel[s].Values[v] = (acc += pixel[s].Values[v]);
				}
				for (size_t s = pixel.size ()-1; s > 0; --s)
				{
					for (int v = 0; v < 4; ++v)
						pixel[s].Values[v] = (pixel[s].Values[v]-pixel[s-1].Values[v])/(1.f-pixel[s-1].Values[TestA]);
				}
			}
		}

		// Read a mask
		cout << "Read the mask" << endl;
		Mask mask;
		mask.read (filename);

		const int R = mask.findSlice ("R");
		const int G = mask.findSlice ("G");
		const int B = mask.findSlice ("B");
		const int A = mask.findSlice ("A");

		// Check the mask
		cout << "Check the mask" << endl;
		// Build the mask of every name
		for (int i = 0; i < NameN; ++i)
		{
			// Create a query for this name
			Query query (&mask, TestMatch (names, i));

			for (int y=0; y<Height; ++y)
			for (int x=0; x<Width; ++x)
			{
				// Recompute the coverage from the original image
				const vector<entry> &pixel = pixelToNames[x+y*Width];
				float weightSum[4] = {0, 0, 0, 0};

				// Fill the accumulated buffer
				float prevAlpha = 0.f;
				for (size_t s = 0; s < pixel.size (); ++s)
				{
					if (pixel[s].Id == i)
					{
						for (int v = 0; v < 4; ++v)
							weightSum[v] += (half)((1.f - prevAlpha)*(half)pixel[s].Values[v]);
					}
					prevAlpha += (1.f - prevAlpha)*(half)pixel[s].Values[TestA];
				}

				// The coverage from the file
				std::vector<float> coverage;
				query.getSliceData (x, y, coverage);

				Errors += int(weightSum[0] != coverage[R]);
				Errors += int(weightSum[1] != coverage[G]);
				Errors += int(weightSum[2] != coverage[B]);
				Errors += int(weightSum[3] != coverage[A]);
			}
		}
	}
	catch (runtime_error &e)
	{
		cout << e.what () << endl;
		remove (filename);
		return -1;
	}

	remove (filename);
	cout << Errors << " error(s)" << endl;

	return Errors;
}

