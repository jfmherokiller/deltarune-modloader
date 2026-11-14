// xxh64.hpp - streaming XXH64 (Yann Collet's xxHash, 64-bit variant), header-only, no
// dependencies. Non-cryptographic: used for cache keys and integrity checks, where speed matters
// (hundreds of MB per launch) and nobody is attacking the hash. Output matches the reference
// implementation (XXH64("", 0) == 0xEF46DB3751D8E999).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace modloader
{
	class Xxh64
	{
	public:
		explicit Xxh64(uint64_t seed = 0) { Reset(seed); }

		void Reset(uint64_t seed = 0)
		{
			m_v[0] = seed + P1 + P2;
			m_v[1] = seed + P2;
			m_v[2] = seed;
			m_v[3] = seed - P1;
			m_seed = seed;
			m_total = 0;
			m_used = 0;
		}

		void Update(const void* data, size_t len)
		{
			const unsigned char* p = static_cast<const unsigned char*>(data);
			m_total += len;
			if (m_used + len < 32) {
				memcpy(m_buf + m_used, p, len);
				m_used += len;
				return;
			}
			if (m_used) {
				size_t fill = 32 - m_used;
				memcpy(m_buf + m_used, p, fill);
				Stripe(m_buf);
				p += fill; len -= fill; m_used = 0;
			}
			while (len >= 32) { Stripe(p); p += 32; len -= 32; }
			if (len) { memcpy(m_buf, p, len); m_used = len; }
		}

		uint64_t Digest() const
		{
			uint64_t h;
			if (m_total >= 32) {
				h = Rotl(m_v[0], 1) + Rotl(m_v[1], 7) + Rotl(m_v[2], 12) + Rotl(m_v[3], 18);
				for (int i = 0; i < 4; i++) h = MergeRound(h, m_v[i]);
			} else {
				h = m_seed + P5;
			}
			h += m_total;
			const unsigned char* p = m_buf;
			size_t len = m_used;
			while (len >= 8) { h ^= Round(0, Read64(p)); h = Rotl(h, 27) * P1 + P4; p += 8; len -= 8; }
			if (len >= 4) { h ^= (uint64_t)Read32(p) * P1; h = Rotl(h, 23) * P2 + P3; p += 4; len -= 4; }
			while (len--) { h ^= (*p++) * P5; h = Rotl(h, 11) * P1; }
			h ^= h >> 33; h *= P2;
			h ^= h >> 29; h *= P3;
			h ^= h >> 32;
			return h;
		}

		static uint64_t Hash(const void* data, size_t len, uint64_t seed = 0)
		{
			Xxh64 x(seed);
			x.Update(data, len);
			return x.Digest();
		}

	private:
		static constexpr uint64_t P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full,
			P3 = 0x165667B19E3779F9ull, P4 = 0x85EBCA77C2B2AE63ull, P5 = 0x27D4EB2F165667C5ull;

		static uint64_t Rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
		static uint64_t Read64(const unsigned char* p) { uint64_t v; memcpy(&v, p, 8); return v; }   // little-endian hosts
		static uint32_t Read32(const unsigned char* p) { uint32_t v; memcpy(&v, p, 4); return v; }
		static uint64_t Round(uint64_t acc, uint64_t in) { acc += in * P2; acc = Rotl(acc, 31); return acc * P1; }
		static uint64_t MergeRound(uint64_t acc, uint64_t v) { acc ^= Round(0, v); return acc * P1 + P4; }

		void Stripe(const unsigned char* p)
		{
			m_v[0] = Round(m_v[0], Read64(p));
			m_v[1] = Round(m_v[1], Read64(p + 8));
			m_v[2] = Round(m_v[2], Read64(p + 16));
			m_v[3] = Round(m_v[3], Read64(p + 24));
		}

		uint64_t m_v[4], m_seed, m_total;
		unsigned char m_buf[32];
		size_t m_used;
	};
}
