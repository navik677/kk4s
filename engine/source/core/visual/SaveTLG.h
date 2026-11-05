
#ifndef LoadTLGH
#define LoadTLGH

#define SLIDE_N 4096
#define SLIDE_M (18+255)
class SlideCompressor
{
	// スライド辞書法 圧縮クラス
	struct Chain
	{
		int Prev;
		int Next;
	};

	unsigned char Text[SLIDE_N + SLIDE_M - 1];
	int Map[256*256];
	Chain Chains[SLIDE_N];


	unsigned char Text2[SLIDE_N + SLIDE_M - 1];
	int Map2[256*256];
	Chain Chains2[SLIDE_N];


	int S;
	int S2;

public:
	SlideCompressor();
	virtual ~SlideCompressor();

private:
	int GetMatch(const unsigned char*cur, int curlen, int &pos, int s);
	void AddMap(int p);
	void DeleteMap(int p);

public:
	void Encode(const unsigned char *in, long inlen,
		unsigned char *out, long & outlen);

	void Store();
	void Restore();
};

// kk4s: krkrs keeps 32bpp bitmaps as RGBA bytes (uint32 0xAABBGGRR), while a TLG
// file stores Kirikiri's BGRA. The TLG writers read their rows through this: a
// BGRA copy for 32bpp images, the bitmap itself for 8bpp ones. Without it a saved
// TLG came back with red and blue swapped.
#include <vector>
class tTVPTLGSourceLines
{
	const class iTVPBaseBitmap *Bmp;
	std::vector<unsigned char> Data;
	size_t Stride;
public:
	explicit tTVPTLGSourceLines(const class iTVPBaseBitmap *bmp);
	const unsigned char * operator()(int y) const;
};

#endif
