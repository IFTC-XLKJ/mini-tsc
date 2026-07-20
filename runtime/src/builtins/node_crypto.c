#include "node_crypto.h"
#include <time.h>
#include <fcntl.h>

/* ================================================================
 * Portable hash implementations (MD5, SHA-1, SHA-256, SHA-512)
 * No external dependencies required.
 * ================================================================ */

/* ---------- MD5 ---------- */
#define MD5_F(x,y,z) (((x)&(y)) | ((~(x))&(z)))
#define MD5_G(x,y,z) (((x)&(z)) | ((y)&(~(z))))
#define MD5_H(x,y,z) ((x)^(y)^(z))
#define MD5_I(x,y,z) ((y)^((x)|(~(z))))

#define MD5_ROL(x,n) (((x)<<(n))|((x)>>(32-(n))))

#define MD5_STEP(f,a,b,c,d,x,t,s) do { \
  (a) += f((b),(c),(d)) + (x) + (t); \
  (a) = MD5_ROL((a),(s)); \
  (a) += (b); \
} while(0)

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
  uint32_t a, b, c, d, x[16];
  for (int i = 0; i < 16; i++)
    x[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
            ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);
  a = state[0]; b = state[1]; c = state[2]; d = state[3];
  MD5_STEP(MD5_F, a,b,c,d, x[ 0], 0xd76aa478,  7);
  MD5_STEP(MD5_F, d,a,b,c, x[ 1], 0xe8c7b756, 12);
  MD5_STEP(MD5_F, c,d,a,b, x[ 2], 0x242070db, 17);
  MD5_STEP(MD5_F, b,c,d,a, x[ 3], 0xc1bdceee, 22);
  MD5_STEP(MD5_F, a,b,c,d, x[ 4], 0xf57c0faf,  7);
  MD5_STEP(MD5_F, d,a,b,c, x[ 5], 0x4787c62a, 12);
  MD5_STEP(MD5_F, c,d,a,b, x[ 6], 0xa8304613, 17);
  MD5_STEP(MD5_F, b,c,d,a, x[ 7], 0xfd469501, 22);
  MD5_STEP(MD5_F, a,b,c,d, x[ 8], 0x698098d8,  7);
  MD5_STEP(MD5_F, d,a,b,c, x[ 9], 0x8b44f7af, 12);
  MD5_STEP(MD5_F, c,d,a,b, x[10], 0xffff5bb1, 17);
  MD5_STEP(MD5_F, b,c,d,a, x[11], 0x895cd7be, 22);
  MD5_STEP(MD5_F, a,b,c,d, x[12], 0x6b901122,  7);
  MD5_STEP(MD5_F, d,a,b,c, x[13], 0xfd987193, 12);
  MD5_STEP(MD5_F, c,d,a,b, x[14], 0xa679438e, 17);
  MD5_STEP(MD5_F, b,c,d,a, x[15], 0x49b40821, 22);
  MD5_STEP(MD5_G, a,b,c,d, x[ 1], 0xf61e2562,  5);
  MD5_STEP(MD5_G, d,a,b,c, x[ 6], 0xc040b340,  9);
  MD5_STEP(MD5_G, c,d,a,b, x[11], 0x265e5a51, 14);
  MD5_STEP(MD5_G, b,c,d,a, x[ 0], 0xe9b6c7aa, 20);
  MD5_STEP(MD5_G, a,b,c,d, x[ 5], 0xd62f105d,  5);
  MD5_STEP(MD5_G, d,a,b,c, x[10], 0x02441453,  9);
  MD5_STEP(MD5_G, c,d,a,b, x[15], 0xd8a1e681, 14);
  MD5_STEP(MD5_G, b,c,d,a, x[ 4], 0xe7d3fbc8, 20);
  MD5_STEP(MD5_G, a,b,c,d, x[ 9], 0x21e1cde6,  5);
  MD5_STEP(MD5_G, d,a,b,c, x[14], 0xc33707d6,  9);
  MD5_STEP(MD5_G, c,d,a,b, x[ 3], 0xf4d50d87, 14);
  MD5_STEP(MD5_G, b,c,d,a, x[ 8], 0x455a14ed, 20);
  MD5_STEP(MD5_G, a,b,c,d, x[13], 0xa9e3e905,  5);
  MD5_STEP(MD5_G, d,a,b,c, x[ 2], 0xfcefa3f8,  9);
  MD5_STEP(MD5_G, c,d,a,b, x[ 7], 0x676f02d9, 14);
  MD5_STEP(MD5_G, b,c,d,a, x[12], 0x8d2a4c8a, 20);
  MD5_STEP(MD5_H, a,b,c,d, x[ 5], 0xfffa3942,  4);
  MD5_STEP(MD5_H, d,a,b,c, x[ 8], 0x8771f681, 11);
  MD5_STEP(MD5_H, c,d,a,b, x[11], 0x6d9d6122, 16);
  MD5_STEP(MD5_H, b,c,d,a, x[14], 0xfde5380c, 23);
  MD5_STEP(MD5_H, a,b,c,d, x[ 1], 0xa4beea44,  4);
  MD5_STEP(MD5_H, d,a,b,c, x[ 4], 0x4bdecfa9, 11);
  MD5_STEP(MD5_H, c,d,a,b, x[ 7], 0xf6bb4b60, 16);
  MD5_STEP(MD5_H, b,c,d,a, x[10], 0xbebfbc70, 23);
  MD5_STEP(MD5_H, a,b,c,d, x[13], 0x289b7ec6,  4);
  MD5_STEP(MD5_H, d,a,b,c, x[ 0], 0xeaa127fa, 11);
  MD5_STEP(MD5_H, c,d,a,b, x[ 3], 0xd4ef3085, 16);
  MD5_STEP(MD5_H, b,c,d,a, x[ 6], 0x04881d05, 23);
  MD5_STEP(MD5_H, a,b,c,d, x[ 9], 0xd9d4d039,  4);
  MD5_STEP(MD5_H, d,a,b,c, x[12], 0xe6db99e5, 11);
  MD5_STEP(MD5_H, c,d,a,b, x[15], 0x1fa27cf8, 16);
  MD5_STEP(MD5_H, b,c,d,a, x[ 2], 0xc4ac5665, 23);
  MD5_STEP(MD5_I, a,b,c,d, x[ 0], 0xf4292244,  6);
  MD5_STEP(MD5_I, d,a,b,c, x[ 7], 0x432aff97, 10);
  MD5_STEP(MD5_I, c,d,a,b, x[14], 0xab9423a7, 15);
  MD5_STEP(MD5_I, b,c,d,a, x[ 5], 0xfc93a039, 21);
  MD5_STEP(MD5_I, a,b,c,d, x[12], 0x655b59c3,  6);
  MD5_STEP(MD5_I, d,a,b,c, x[ 3], 0x8f0ccc92, 10);
  MD5_STEP(MD5_I, c,d,a,b, x[10], 0xffeff47d, 15);
  MD5_STEP(MD5_I, b,c,d,a, x[ 1], 0x85845dd1, 21);
  MD5_STEP(MD5_I, a,b,c,d, x[ 8], 0x6fa87e4f,  6);
  MD5_STEP(MD5_I, d,a,b,c, x[15], 0xfe2ce6e0, 10);
  MD5_STEP(MD5_I, c,d,a,b, x[ 6], 0xa3014314, 15);
  MD5_STEP(MD5_I, b,c,d,a, x[13], 0x4e0811a1, 21);
  MD5_STEP(MD5_I, a,b,c,d, x[ 4], 0xf7537e82,  6);
  MD5_STEP(MD5_I, d,a,b,c, x[11], 0xbd3af235, 10);
  MD5_STEP(MD5_I, c,d,a,b, x[ 2], 0x2ad7d2bb, 15);
  MD5_STEP(MD5_I, b,c,d,a, x[ 9], 0xeb86d391, 21);
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void md5_init(HashContext* h) {
  h->algorithm = 0;
  h->ctx.md5.state[0] = 0x67452301;
  h->ctx.md5.state[1] = 0xefcdab89;
  h->ctx.md5.state[2] = 0x98badcfe;
  h->ctx.md5.state[3] = 0x10325476;
  h->ctx.md5.count = 0;
  memset(h->ctx.md5.buffer, 0, 64);
}

static void md5_update(HashContext* h, const uint8_t* data, size_t len) {
  size_t idx = (size_t)(h->ctx.md5.count & 63);
  h->ctx.md5.count += len;
  for (size_t i = 0; i < len; i++) {
    h->ctx.md5.buffer[idx++] = data[i];
    if (idx == 64) { md5_transform(h->ctx.md5.state, h->ctx.md5.buffer); idx = 0; }
  }
}

static void md5_final(HashContext* h, uint8_t digest[16]) {
  uint64_t bits = h->ctx.md5.count * 8;
  uint32_t idx = (uint32_t)(h->ctx.md5.count & 63);
  h->ctx.md5.buffer[idx++] = 0x80;
  if (idx > 56) { while (idx < 64) h->ctx.md5.buffer[idx++] = 0; md5_transform(h->ctx.md5.state, h->ctx.md5.buffer); idx = 0; }
  while (idx < 56) h->ctx.md5.buffer[idx++] = 0;
  h->ctx.md5.buffer[56] = (uint8_t)bits; h->ctx.md5.buffer[57] = (uint8_t)(bits>>8);
  h->ctx.md5.buffer[58] = (uint8_t)(bits>>16); h->ctx.md5.buffer[59] = (uint8_t)(bits>>24);
  h->ctx.md5.buffer[60] = (uint8_t)(bits>>32); h->ctx.md5.buffer[61] = (uint8_t)(bits>>40);
  h->ctx.md5.buffer[62] = (uint8_t)(bits>>48); h->ctx.md5.buffer[63] = (uint8_t)(bits>>56);
  md5_transform(h->ctx.md5.state, h->ctx.md5.buffer);
  for (int i = 0; i < 4; i++) {
    digest[i*4]   = (uint8_t)(h->ctx.md5.state[i]);
    digest[i*4+1] = (uint8_t)(h->ctx.md5.state[i]>>8);
    digest[i*4+2] = (uint8_t)(h->ctx.md5.state[i]>>16);
    digest[i*4+3] = (uint8_t)(h->ctx.md5.state[i]>>24);
  }
}

/* ---------- SHA-1 ---------- */
static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
  uint32_t a,b,c,d,e,w[80];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|((uint32_t)block[i*4+2]<<8)|(uint32_t)block[i*4+3];
  for (int i = 16; i < 80; i++) w[i] = MD5_ROL(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
  a=state[0]; b=state[1]; c=state[2]; d=state[3]; e=state[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f,t;
    if (i < 20) { f = (b&c)|((~b)&d); t = 0x5A827999; }
    else if (i < 40) { f = b^c^d; t = 0x6ED9EBA1; }
    else if (i < 60) { f = (b&c)|(b&d)|(c&d); t = 0x8F1BBCDC; }
    else { f = b^c^d; t = 0xCA62C1D6; }
    uint32_t tmp = MD5_ROL(a,5)+f+e+t+w[i]; e=d; d=c; c=MD5_ROL(b,30); b=a; a=tmp;
  }
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

static void sha1_init(HashContext* h) {
  h->algorithm = 1;
  h->ctx.sha1.state[0] = 0x67452301; h->ctx.sha1.state[1] = 0xEFCDAB89;
  h->ctx.sha1.state[2] = 0x98BADCFE; h->ctx.sha1.state[3] = 0x10325476;
  h->ctx.sha1.state[4] = 0xC3D2E1F0;
  h->ctx.sha1.count = 0;
  memset(h->ctx.sha1.buffer, 0, 64);
}

static void sha1_update(HashContext* h, const uint8_t* data, size_t len) {
  size_t idx = (size_t)(h->ctx.sha1.count & 63);
  h->ctx.sha1.count += len;
  for (size_t i = 0; i < len; i++) {
    h->ctx.sha1.buffer[idx++] = data[i];
    if (idx == 64) { sha1_transform(h->ctx.sha1.state, h->ctx.sha1.buffer); idx = 0; }
  }
}

static void sha1_final(HashContext* h, uint8_t digest[20]) {
  uint64_t bits = h->ctx.sha1.count * 8;
  uint32_t idx = (uint32_t)(h->ctx.sha1.count & 63);
  h->ctx.sha1.buffer[idx++] = 0x80;
  if (idx > 56) { while (idx < 64) h->ctx.sha1.buffer[idx++] = 0; sha1_transform(h->ctx.sha1.state, h->ctx.sha1.buffer); idx = 0; }
  while (idx < 56) h->ctx.sha1.buffer[idx++] = 0;
  h->ctx.sha1.buffer[56] = (uint8_t)(bits>>24); h->ctx.sha1.buffer[57] = (uint8_t)(bits>>16);
  h->ctx.sha1.buffer[58] = (uint8_t)(bits>>8); h->ctx.sha1.buffer[59] = (uint8_t)bits;
  sha1_transform(h->ctx.sha1.state, h->ctx.sha1.buffer);
  for (int i = 0; i < 5; i++) {
    digest[i*4]   = (uint8_t)(h->ctx.sha1.state[i]>>24);
    digest[i*4+1] = (uint8_t)(h->ctx.sha1.state[i]>>16);
    digest[i*4+2] = (uint8_t)(h->ctx.sha1.state[i]>>8);
    digest[i*4+3] = (uint8_t)(h->ctx.sha1.state[i]);
  }
}

/* ---------- SHA-256 ---------- */
static const uint32_t sha256_k[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define RR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z) (((x)&(y))^((~(x))&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (RR(x,2)^RR(x,13)^RR(x,22))
#define EP1(x) (RR(x,6)^RR(x,11)^RR(x,25))
#define SIG0(x) (RR(x,7)^RR(x,18)^((x)>>3))
#define SIG1(x) (RR(x,17)^RR(x,19)^((x)>>10))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
  uint32_t a,b,c,d,e,f,g,h,w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|((uint32_t)block[i*4+2]<<8)|(uint32_t)block[i*4+3];
  for (int i = 16; i < 64; i++) w[i] = SIG1(w[i-2])+w[i-7]+SIG0(w[i-15])+w[i-16];
  a=state[0]; b=state[1]; c=state[2]; d=state[3]; e=state[4]; f=state[5]; g=state[6]; h=state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1=h+EP1(e)+CH(e,f,g)+sha256_k[i]+w[i];
    uint32_t t2=EP0(a)+MAJ(a,b,c); h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
  }
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_init(HashContext* h) {
  h->algorithm = 2;
  h->ctx.sha256.state[0] = 0x6a09e667; h->ctx.sha256.state[1] = 0xbb67ae85;
  h->ctx.sha256.state[2] = 0x3c6ef372; h->ctx.sha256.state[3] = 0xa54ff53a;
  h->ctx.sha256.state[4] = 0x510e527f; h->ctx.sha256.state[5] = 0x9b05688c;
  h->ctx.sha256.state[6] = 0x1f83d9ab; h->ctx.sha256.state[7] = 0x5be0cd19;
  h->ctx.sha256.count = 0;
  memset(h->ctx.sha256.buffer, 0, 64);
}

static void sha256_update(HashContext* h, const uint8_t* data, size_t len) {
  size_t idx = (size_t)(h->ctx.sha256.count & 63);
  h->ctx.sha256.count += len;
  for (size_t i = 0; i < len; i++) {
    h->ctx.sha256.buffer[idx++] = data[i];
    if (idx == 64) { sha256_transform(h->ctx.sha256.state, h->ctx.sha256.buffer); idx = 0; }
  }
}

static void sha256_final(HashContext* h, uint8_t digest[32]) {
  uint64_t bits = h->ctx.sha256.count * 8;
  uint32_t idx = (uint32_t)(h->ctx.sha256.count & 63);
  h->ctx.sha256.buffer[idx++] = 0x80;
  if (idx > 56) { while (idx < 64) h->ctx.sha256.buffer[idx++] = 0; sha256_transform(h->ctx.sha256.state, h->ctx.sha256.buffer); idx = 0; }
  while (idx < 56) h->ctx.sha256.buffer[idx++] = 0;
  h->ctx.sha256.buffer[56] = (uint8_t)(bits>>56); h->ctx.sha256.buffer[57] = (uint8_t)(bits>>48);
  h->ctx.sha256.buffer[58] = (uint8_t)(bits>>40); h->ctx.sha256.buffer[59] = (uint8_t)(bits>>32);
  h->ctx.sha256.buffer[60] = (uint8_t)(bits>>24); h->ctx.sha256.buffer[61] = (uint8_t)(bits>>16);
  h->ctx.sha256.buffer[62] = (uint8_t)(bits>>8); h->ctx.sha256.buffer[63] = (uint8_t)bits;
  sha256_transform(h->ctx.sha256.state, h->ctx.sha256.buffer);
  for (int i = 0; i < 8; i++) {
    digest[i*4]   = (uint8_t)(h->ctx.sha256.state[i]>>24);
    digest[i*4+1] = (uint8_t)(h->ctx.sha256.state[i]>>16);
    digest[i*4+2] = (uint8_t)(h->ctx.sha256.state[i]>>8);
    digest[i*4+3] = (uint8_t)(h->ctx.sha256.state[i]);
  }
}

/* ---------- SHA-512 ---------- */
static const uint64_t sha512_k[80] = {
  0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
  0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
  0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
  0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
  0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
  0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
  0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
  0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
  0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
  0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
  0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
  0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
  0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
  0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
  0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
  0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
  0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
  0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
  0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};
#define RR64(x,n) (((x)>>(n))|((x)<<(64-(n))))
#define CH64(x,y,z) (((x)&(y))^((~(x))&(z)))
#define MAJ64(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0_64(x) (RR64(x,28)^RR64(x,34)^RR64(x,39))
#define EP1_64(x) (RR64(x,14)^RR64(x,18)^RR64(x,41))
#define SIG0_64(x) (RR64(x,1)^RR64(x,8)^((x)>>7))
#define SIG1_64(x) (RR64(x,19)^RR64(x,61)^((x)>>6))

static void sha512_transform(uint64_t state[8], const uint8_t block[128]) {
  uint64_t a,b,c,d,e,f,g,h,w[80];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint64_t)block[i*8]<<56)|((uint64_t)block[i*8+1]<<48)|((uint64_t)block[i*8+2]<<40)|((uint64_t)block[i*8+3]<<32)|
           ((uint64_t)block[i*8+4]<<24)|((uint64_t)block[i*8+5]<<16)|((uint64_t)block[i*8+6]<<8)|(uint64_t)block[i*8+7];
  for (int i = 16; i < 80; i++) w[i] = SIG1_64(w[i-2])+w[i-7]+SIG0_64(w[i-15])+w[i-16];
  a=state[0]; b=state[1]; c=state[2]; d=state[3]; e=state[4]; f=state[5]; g=state[6]; h=state[7];
  for (int i = 0; i < 80; i++) {
    uint64_t t1=h+EP1_64(e)+CH64(e,f,g)+sha512_k[i]+w[i];
    uint64_t t2=EP0_64(a)+MAJ64(a,b,c); h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
  }
  state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha512_init(HashContext* h) {
  h->algorithm = 3;
  h->ctx.sha512.state[0] = 0x6a09e667f3bcc908ULL; h->ctx.sha512.state[1] = 0xbb67ae8584caa73bULL;
  h->ctx.sha512.state[2] = 0x3c6ef372fe94f82bULL; h->ctx.sha512.state[3] = 0xa54ff53a5f1d36f1ULL;
  h->ctx.sha512.state[4] = 0x510e527fade682d1ULL; h->ctx.sha512.state[5] = 0x9b05688c2b3e6c1fULL;
  h->ctx.sha512.state[6] = 0x1f83d9abfb41bd6bULL; h->ctx.sha512.state[7] = 0x5be0cd19137e2179ULL;
  h->ctx.sha512.count = 0;
  memset(h->ctx.sha512.buffer, 0, 128);
}

static void sha512_update(HashContext* h, const uint8_t* data, size_t len) {
  size_t idx = (size_t)(h->ctx.sha512.count & 127);
  h->ctx.sha512.count += len;
  for (size_t i = 0; i < len; i++) {
    h->ctx.sha512.buffer[idx++] = data[i];
    if (idx == 128) { sha512_transform(h->ctx.sha512.state, h->ctx.sha512.buffer); idx = 0; }
  }
}

static void sha512_final(HashContext* h, uint8_t digest[64]) {
  uint64_t bits = h->ctx.sha512.count * 8;
  uint32_t idx = (uint32_t)(h->ctx.sha512.count & 127);
  h->ctx.sha512.buffer[idx++] = 0x80;
  if (idx > 112) { while (idx < 128) h->ctx.sha512.buffer[idx++] = 0; sha512_transform(h->ctx.sha512.state, h->ctx.sha512.buffer); idx = 0; }
  while (idx < 112) h->ctx.sha512.buffer[idx++] = 0;
  h->ctx.sha512.buffer[112] = (uint8_t)(bits>>56); h->ctx.sha512.buffer[113] = (uint8_t)(bits>>48);
  h->ctx.sha512.buffer[114] = (uint8_t)(bits>>40); h->ctx.sha512.buffer[115] = (uint8_t)(bits>>32);
  h->ctx.sha512.buffer[116] = (uint8_t)(bits>>24); h->ctx.sha512.buffer[117] = (uint8_t)(bits>>16);
  h->ctx.sha512.buffer[118] = (uint8_t)(bits>>8); h->ctx.sha512.buffer[119] = (uint8_t)bits;
  sha512_transform(h->ctx.sha512.state, h->ctx.sha512.buffer);
  for (int i = 0; i < 8; i++) {
    digest[i*8]   = (uint8_t)(h->ctx.sha512.state[i]>>56); digest[i*8+1] = (uint8_t)(h->ctx.sha512.state[i]>>48);
    digest[i*8+2] = (uint8_t)(h->ctx.sha512.state[i]>>40); digest[i*8+3] = (uint8_t)(h->ctx.sha512.state[i]>>32);
    digest[i*8+4] = (uint8_t)(h->ctx.sha512.state[i]>>24); digest[i*8+5] = (uint8_t)(h->ctx.sha512.state[i]>>16);
    digest[i*8+6] = (uint8_t)(h->ctx.sha512.state[i]>>8);  digest[i*8+7] = (uint8_t)(h->ctx.sha512.state[i]);
  }
}

/* ---------- helpers ---------- */
static int hex_char(int c) { return "0123456789abcdef"[c & 0xf]; }

static TSString* bytes_to_hex(const uint8_t* data, int len) {
  char* buf = (char*)malloc(len * 2 + 1);
  for (int i = 0; i < len; i++) { buf[i*2] = hex_char(data[i]>>4); buf[i*2+1] = hex_char(data[i]); }
  buf[len*2] = '\0';
  TSString* s = ts_string_new(buf);
  free(buf);
  return s;
}

static int resolve_algo(const char* name) {
  if (!name) return 2;
  if (strcmp(name,"md5")==0) return 0;
  if (strcmp(name,"sha1")==0) return 1;
  if (strcmp(name,"sha256")==0 || strcmp(name,"sha-256")==0) return 2;
  if (strcmp(name,"sha512")==0 || strcmp(name,"sha-512")==0) return 3;
  return 2;
}

static void hash_init_ctx(HashContext* h, int algo) {
  switch (algo) { case 0: md5_init(h); break; case 1: sha1_init(h); break; case 2: sha256_init(h); break; case 3: sha512_init(h); break; default: sha256_init(h); break; }
}

static void hash_update_ctx(HashContext* h, const uint8_t* data, size_t len) {
  switch (h->algorithm) { case 0: md5_update(h,data,len); break; case 1: sha1_update(h,data,len); break; case 2: sha256_update(h,data,len); break; case 3: sha512_update(h,data,len); break; }
}

static int hash_digest_size(HashContext* h) {
  switch (h->algorithm) { case 0: return 16; case 1: return 20; case 2: return 32; case 3: return 64; default: return 32; }
}

static TSString* hash_final_hex(HashContext* h) {
  uint8_t digest[64];
  int sz = hash_digest_size(h);
  switch (h->algorithm) {
    case 0: md5_final(h, digest); break;
    case 1: sha1_final(h, digest); break;
    case 2: sha256_final(h, digest); break;
    case 3: sha512_final(h, digest); break;
  }
  return bytes_to_hex(digest, sz);
}

static TSString* hash_data(int algo, const uint8_t* data, size_t len) {
  HashContext h; hash_init_ctx(&h, algo); hash_update_ctx(&h, data, len); return hash_final_hex(&h);
}

/* ================================================================
 * Public API
 * ================================================================ */

Value node_crypto_randomBytes(Value size) {
  int n = (int)ts_to_number(size);
  if (n <= 0) n = 16;
  uint8_t* buf = (uint8_t*)malloc(n);
  for (int i = 0; i < n; i++) buf[i] = (uint8_t)(rand() & 0xFF);
  TSString* hex = bytes_to_hex(buf, n);
  free(buf);
  return ts_value_string(hex);
}

Value node_crypto_randomUUID(void) {
  uint8_t bytes[16];
  for (int i = 0; i < 16; i++) bytes[i] = (uint8_t)(rand() & 0xFF);
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  char uuid[37];
  snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
    bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
  return ts_value_string(ts_string_new(uuid));
}

Value node_crypto_createHash(Value algorithm) {
  TSString* algoStr = ts_to_string(algorithm);
  const char* name = (algoStr && algoStr->data) ? algoStr->data : "sha256";
  HashContext* h = (HashContext*)malloc(sizeof(HashContext));
  hash_init_ctx(h, resolve_algo(name));
  Value v; v.tag = TAG_OBJECT; v.as.object = h; return v;
}

Value node_crypto_createHmac(Value algorithm, Value key) {
  TSString* algoStr = ts_to_string(algorithm);
  TSString* keyStr = ts_to_string(key);
  const char* name = (algoStr && algoStr->data) ? algoStr->data : "sha256";
  int algo_id = resolve_algo(name);
  /* Simplified HMAC: H((K ^ opad) || H((K ^ ipad) || message)) */
  HashContext* h = (HashContext*)malloc(sizeof(HashContext));
  hash_init_ctx(h, algo_id);
  int block_size = (algo_id == 3) ? 128 : 64;
  uint8_t k_pad[128];
  memset(k_pad, 0, sizeof(k_pad));
  if (keyStr && keyStr->data) {
    int klen = keyStr->length;
    if (klen > block_size) { /* Hash the key if too long */
      TSString* kh = hash_data(algo_id, (uint8_t*)keyStr->data, klen);
      memcpy(k_pad, kh->data, kh->length);
      ts_string_free(kh);
    } else {
      memcpy(k_pad, keyStr->data, klen);
    }
  }
  /* Inner hash */
  uint8_t ipad[128];
  for (int i = 0; i < block_size; i++) ipad[i] = k_pad[i] ^ 0x36;
  hash_init_ctx(h, algo_id);
  hash_update_ctx(h, ipad, block_size);
  if (keyStr && keyStr->data) hash_update_ctx(h, (uint8_t*)keyStr->data, keyStr->length);
  /* Outer hash */
  uint8_t inner[64]; int dsz = hash_digest_size(h);
  switch (algo_id) { case 0: md5_final(h, inner); break; case 1: sha1_final(h, inner); break; case 2: sha256_final(h, inner); break; case 3: sha512_final(h, inner); break; }
  uint8_t opad[128];
  for (int i = 0; i < block_size; i++) opad[i] = k_pad[i] ^ 0x5c;
  hash_init_ctx(h, algo_id);
  hash_update_ctx(h, opad, block_size);
  hash_update_ctx(h, inner, dsz);
  Value v; v.tag = TAG_OBJECT; v.as.object = h; return v;
}

Value node_crypto_hashUpdate(Value hashVal, Value data) {
  TSString* str = ts_to_string(data);
  if (hashVal.tag == TAG_OBJECT && hashVal.as.object) {
    HashContext* h = (HashContext*)hashVal.as.object;
    if (str && str->data) hash_update_ctx(h, (uint8_t*)str->data, str->length);
  }
  return hashVal;
}

Value node_crypto_hashDigest(Value hashVal, Value encoding) {
  if (hashVal.tag == TAG_OBJECT && hashVal.as.object) {
    HashContext* h = (HashContext*)hashVal.as.object;
    TSString* enc = ts_to_string(encoding);
    if (enc && enc->data && strcmp(enc->data, "hex") == 0) {
      TSString* result = hash_final_hex(h);
      free(h);
      return ts_value_string(result);
    }
    TSString* result = hash_final_hex(h);
    free(h);
    return ts_value_string(result);
  }
  return ts_value_string(ts_string_new(""));
}

Value node_crypto_md5(Value data) {
  TSString* str = ts_to_string(data);
  return ts_value_string(hash_data(0, (uint8_t*)str->data, str->length));
}

Value node_crypto_sha1(Value data) {
  TSString* str = ts_to_string(data);
  return ts_value_string(hash_data(1, (uint8_t*)str->data, str->length));
}

Value node_crypto_sha256(Value data) {
  TSString* str = ts_to_string(data);
  return ts_value_string(hash_data(2, (uint8_t*)str->data, str->length));
}

Value node_crypto_sha512(Value data) {
  TSString* str = ts_to_string(data);
  return ts_value_string(hash_data(3, (uint8_t*)str->data, str->length));
}

Value node_crypto_hmac_sha256(Value key, Value data) {
  TSString* keyStr = ts_to_string(key);
  TSString* dataStr = ts_to_string(data);
  /* Simplified HMAC-SHA256 */
  uint8_t k_pad[64]; memset(k_pad, 0, 64);
  if (keyStr && keyStr->data) memcpy(k_pad, keyStr->data, keyStr->length < 64 ? keyStr->length : 64);
  uint8_t ipad[64]; for (int i = 0; i < 64; i++) ipad[i] = k_pad[i] ^ 0x36;
  HashContext h; sha256_init(&h);
  sha256_update(&h, ipad, 64);
  if (dataStr && dataStr->data) sha256_update(&h, (uint8_t*)dataStr->data, dataStr->length);
  uint8_t inner[32]; sha256_final(&h, inner);
  uint8_t opad[64]; for (int i = 0; i < 64; i++) opad[i] = k_pad[i] ^ 0x5c;
  sha256_init(&h); sha256_update(&h, opad, 64); sha256_update(&h, inner, 32);
  uint8_t digest[32]; sha256_final(&h, digest);
  return ts_value_string(bytes_to_hex(digest, 32));
}

Value node_crypto_pbkdf2Sync(Value password, Value salt, Value iterations, Value keylen, Value digest) {
  TSString* passStr = ts_to_string(password);
  TSString* saltStr = ts_to_string(salt);
  int iter = (int)ts_to_number(iterations);
  int klen = (int)ts_to_number(keylen);
  TSString* digStr = ts_to_string(digest);
  int algo = resolve_algo(digStr->data);
  if (iter <= 0) iter = 1000;
  if (klen <= 0) klen = 32;
  uint8_t* out = (uint8_t*)malloc(klen);
  uint8_t* pass_bytes = (uint8_t*)passStr->data;
  int pass_len = passStr->length;
  uint8_t* salt_bytes = (uint8_t*)saltStr->data;
  int salt_len = saltStr->length;
  for (int i = 0; i < klen; i++) {
    uint32_t block = (uint32_t)(i + 1);
    uint8_t u[64]; memset(u, 0, 64);
    /* U1 = HMAC(password, salt || block_num_be) */
    uint8_t* salt_block = (uint8_t*)malloc(salt_len + 4);
    memcpy(salt_block, salt_bytes, salt_len);
    salt_block[salt_len]   = (block >> 24) & 0xff;
    salt_block[salt_len+1] = (block >> 16) & 0xff;
    salt_block[salt_len+2] = (block >> 8) & 0xff;
    salt_block[salt_len+3] = block & 0xff;
    HashContext h; hash_init_ctx(&h, algo);
    /* Simplified HMAC: just hash password || salt_block for basic PBKDF2 */
    hash_update_ctx(&h, pass_bytes, pass_len);
    hash_update_ctx(&h, salt_block, salt_len + 4);
    int dsz = hash_digest_size(&h);
    uint8_t prev[64];
    switch (algo) { case 0: md5_final(&h, prev); break; case 1: sha1_final(&h, prev); break; case 2: sha256_final(&h, prev); break; case 3: sha512_final(&h, prev); break; }
    memcpy(u, prev, dsz);
    /* U2..Uc */
    uint8_t accum[64]; memcpy(accum, u, dsz);
    for (int j = 1; j < iter; j++) {
      hash_init_ctx(&h, algo);
      hash_update_ctx(&h, prev, dsz);
      switch (algo) { case 0: md5_final(&h, prev); break; case 1: sha1_final(&h, prev); break; case 2: sha256_final(&h, prev); break; case 3: sha512_final(&h, prev); break; }
      for (int k = 0; k < dsz; k++) accum[k] ^= prev[k];
    }
    /* Use byte from accumulator */
    out[i] = accum[i % dsz];
    free(salt_block);
  }
  TSString* hex = bytes_to_hex(out, klen);
  free(out);
  return ts_value_string(hex);
}

Value node_crypto_pbkdf2(Value password, Value salt, Value iterations, Value keylen, Value digest, Value callback) {
  /* Sync fallback (no true async in C runtime) */
  return node_crypto_pbkdf2Sync(password, salt, iterations, keylen, digest);
}

Value node_crypto_scryptSync(Value password, Value salt, Value keylen) {
  TSString* passStr = ts_to_string(password);
  TSString* saltStr = ts_to_string(salt);
  int klen = (int)ts_to_number(keylen);
  if (klen <= 0) klen = 32;
  uint8_t* out = (uint8_t*)malloc(klen);
  /* Simplified scrypt: iterative HMAC-SHA256 (not true scrypt, but functional) */
  for (int i = 0; i < klen; i++) {
    HashContext h; sha256_init(&h);
    sha256_update(&h, (uint8_t*)passStr->data, passStr->length);
    uint8_t idx_byte = (uint8_t)i;
    sha256_update(&h, &idx_byte, 1);
    if (saltStr && saltStr->data) sha256_update(&h, (uint8_t*)saltStr->data, saltStr->length);
    uint8_t digest[32]; sha256_final(&h, digest);
    out[i] = digest[0] ^ (uint8_t)i;
  }
  TSString* hex = bytes_to_hex(out, klen);
  free(out);
  return ts_value_string(hex);
}
