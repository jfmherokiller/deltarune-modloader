// GmlCache.hpp - on-disk cache of the loose-GML compile (ROADMAP 35). Included by ModuleMain.cpp.
//
// The compiler (LooseModCompiler / UTMT CLI) turns a vanilla data.win plus the mods' loose files
// into a patched data.win. That takes seconds per chapter launch, so the result is kept in
//   mods\cache\loosegml\<chapter dir, or "launcher">\data.win      (the compiler's exact bytes)
//   mods\cache\loosegml\<...>\manifest.txt                        (key + artifact size/hash)
// and served again while the key is unchanged. The key is plain text, one input per line:
//   - the cache format and this module (LooseMods.dll content hash);
//   - the compiler: every .exe/.dll/.json in its folder (size + mtime), the import script (content
//     hash) and the command line shape;
//   - the target: chapter dir and vanilla data.win (size + mtime + content hash; the content hash is
//     only recomputed when size or mtime change, like git's index);
//   - each mod root handed to the compiler, in load order, and every file under its code\, sprites\
//     and sounds\ (relative path, size, content hash), sorted ordinal case-insensitive so the key
//     is the same whatever order the file system lists them in (NTFS vs Wine on a Linux FS).
// Paths in the key are relative to mods\ and the game folder, so the key does not depend on where
// the game is installed. A hit needs the whole key to match; the miss log names the first line
// that differs. On a miss the compiler's output is stored from a background thread (the game does
// not wait for the disk): the old manifest is deleted first, the artifact is written to a temp
// file and renamed, and the new manifest goes last, so an interrupted write is a miss, never a
// half-read file. A hit re-checks the artifact's size and XXH64 before the game gets it.
// [loader] gml_cache=0 in modloader.ini, or env DR_GML_CACHE=0, bypasses the cache (no reads, no
// writes); deleting mods\cache clears it.
#pragma once
#include <windows.h>
#include <xxh64.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace gmlcache
{
	using LogFn = void (*)(const char*, ...);
	using AllocFn = void* (*)(size_t);   // must return a buffer the game may own (runner allocator)

	constexpr const char* FORMAT = "DR loose-GML cache v1";

	struct Inputs
	{
		std::wstring gameRoot;       // no trailing slash
		std::wstring modsDir;        // gameRoot\mods
		std::wstring moduleDll;      // LooseMods.dll
		std::wstring compiler;       // exe the loader starts
		std::wstring script;         // -s script (UTMT CLI)
		std::wstring dataWin;        // full path of the vanilla data.win
		std::wstring relDir;         // data.win folder relative to the game root, "." for the launcher
		std::vector<std::wstring> roots;   // mod roots in load order (full paths, as in DR_MOD_DIRS)
	};

	inline std::string U8(const std::wstring& w)
	{
		if (w.empty()) return {};
		int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n > 0 ? n : 0, '\0');
		if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
		return s;
	}

	inline std::string Hex(uint64_t v)
	{
		char b[17];
		snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
		return b;
	}

	// Path below base (case-insensitive prefix), with '/' turned into '\'; the full path otherwise.
	inline std::wstring RelTo(const std::wstring& base, std::wstring p)
	{
		for (auto& c : p) if (c == L'/') c = L'\\';
		if (p.size() > base.size() && _wcsnicmp(p.c_str(), base.c_str(), base.size()) == 0 && p[base.size()] == L'\\')
			return p.substr(base.size() + 1);
		return p;
	}

	inline bool LessName(const std::wstring& a, const std::wstring& b)
	{
		int r = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
		if (r == CSTR_EQUAL) r = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, FALSE);
		return r == CSTR_LESS_THAN;
	}

	inline bool Stat(const std::wstring& path, uint64_t& size, uint64_t& mtime)
	{
		WIN32_FILE_ATTRIBUTE_DATA a;
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			return false;
		size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
		mtime = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
		return true;
	}

	inline bool HashFile(const std::wstring& path, uint64_t& size, uint64_t& hash)
	{
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (h == INVALID_HANDLE_VALUE) return false;
		std::vector<unsigned char> buf(4u << 20);
		modloader::Xxh64 x;
		size = 0;
		DWORD n = 0;
		bool ok = true;
		for (;;) {
			if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &n, nullptr)) { ok = false; break; }
			if (!n) break;
			x.Update(buf.data(), n);
			size += n;
		}
		CloseHandle(h);
		hash = x.Digest();
		return ok;
	}

	// Files under dir (down to depth levels of subfolders), as paths relative to dir; filter(name)
	// picks files. Unsorted.
	template <class F>
	void ListFiles(const std::wstring& dir, const std::wstring& rel, F filter, std::vector<std::wstring>& out, int depth = 16)
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
			std::wstring r = rel.empty() ? std::wstring(fd.cFileName) : rel + L"\\" + fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (depth > 0) ListFiles(dir + L"\\" + fd.cFileName, r, filter, out, depth - 1);
			}
			else if (filter(fd.cFileName)) out.push_back(r);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}

	inline bool EndsWithI(const wchar_t* s, const wchar_t* suffix)
	{
		size_t a = wcslen(s), b = wcslen(suffix);
		return a >= b && _wcsicmp(s + a - b, suffix) == 0;
	}

	inline bool ReadSmallFile(const std::wstring& path, std::string& out)
	{
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE) return false;
		LARGE_INTEGER sz;
		bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < (64 << 20);
		if (ok) {
			out.resize((size_t)sz.QuadPart);
			DWORD n = 0;
			ok = out.empty() || (ReadFile(h, &out[0], (DWORD)out.size(), &n, nullptr) && n == out.size());
		}
		CloseHandle(h);
		return ok;
	}

	inline std::string LineStartingWith(const std::string& text, const std::string& prefix)
	{
		size_t p = 0;
		while (p < text.size()) {
			size_t e = text.find('\n', p);
			if (e == std::string::npos) e = text.size();
			if (text.compare(p, prefix.size(), prefix) == 0) return text.substr(p, e - p);
			p = e + 1;
		}
		return {};
	}

	// Cache folder for one data.win.
	inline std::wstring CacheDir(const Inputs& in)
	{
		std::wstring name = in.relDir == L"." ? L"launcher" : in.relDir;
		for (auto& c : name) if (c == L'\\' || c == L'/' || c == L':') c = L'_';
		return in.modsDir + L"\\cache\\loosegml\\" + name;
	}

	// The key text. prevManifest (may be empty) lets the vanilla data.win content hash be reused
	// when its size and mtime have not changed.
	inline std::string BuildKey(const Inputs& in, const std::string& prevManifest)
	{
		std::string k = std::string(FORMAT) + "\n";
		uint64_t size = 0, mtime = 0, hash = 0;

		if (HashFile(in.moduleDll, size, hash)) k += "loosemods " + std::to_string(size) + " " + Hex(hash) + "\n";
		else k += "loosemods unknown\n";

		// Compiler: stat its whole install (exe, managed dlls, runtime config, GameSpecificData).
		std::wstring cdir = in.compiler.substr(0, in.compiler.find_last_of(L"\\/"));
		k += "compiler " + U8(in.compiler.substr(cdir.size() + (cdir.size() < in.compiler.size() ? 1 : 0))) + "\n";
		std::vector<std::wstring> cfiles;
		ListFiles(cdir, L"", [](const wchar_t* n) {
			return EndsWithI(n, L".exe") || EndsWithI(n, L".dll") || EndsWithI(n, L".json");
		}, cfiles, 2);
		std::sort(cfiles.begin(), cfiles.end(), LessName);
		for (auto& f : cfiles)
			if (Stat(cdir + L"\\" + f, size, mtime))
				k += "compiler-file " + U8(f) + " " + std::to_string(size) + " " + std::to_string(mtime) + "\n";
		if (HashFile(in.script, size, hash)) k += "script " + U8(RelTo(in.gameRoot, in.script)) + " " + std::to_string(size) + " " + Hex(hash) + "\n";
		else k += "script missing\n";
		k += "command load <data.win> -s <script>; env DR_MOD_DIRS DR_OUT_PIPE\n";

		// Target data.win.
		k += "target " + U8(in.relDir) + "\n";
		if (Stat(in.dataWin, size, mtime)) {
			std::string prefix = "data " + std::to_string(size) + " " + std::to_string(mtime) + " ";
			std::string prev = LineStartingWith(prevManifest, prefix);
			if (!prev.empty()) k += prev + "\n";
			else if (HashFile(in.dataWin, size, hash)) k += "data " + std::to_string(size) + " " + std::to_string(mtime) + " " + Hex(hash) + "\n";
			else k += "data unreadable\n";
		} else {
			k += "data missing\n";
		}

		// Mod roots in load order, and every file the importer could read from them.
		for (auto& root : in.roots) {
			k += "root " + U8(RelTo(in.modsDir, root)) + "\n";
			std::vector<std::wstring> files;
			for (const wchar_t* sub : { L"code", L"sprites", L"sounds" })
				ListFiles(root + L"\\" + sub, sub, [](const wchar_t*) { return true; }, files);
			std::sort(files.begin(), files.end(), LessName);
			for (auto& f : files) {
				if (HashFile(root + L"\\" + f, size, hash)) k += "file " + U8(f) + " " + std::to_string(size) + " " + Hex(hash) + "\n";
				else k += "file " + U8(f) + " unreadable\n";
			}
		}
		return k;
	}

	// First line where two key texts differ, for the miss log.
	inline std::string FirstDifference(const std::string& oldKey, const std::string& newKey)
	{
		size_t p = 0;
		while (p < oldKey.size() && p < newKey.size() && oldKey[p] == newKey[p]) p++;
		size_t ls = newKey.rfind('\n', p ? p - 1 : 0);
		ls = (ls == std::string::npos || p == 0) ? 0 : ls + 1;
		auto line = [ls](const std::string& s) {
			if (ls >= s.size()) return std::string("(end)");
			size_t e = s.find('\n', ls);
			std::string l = s.substr(ls, e == std::string::npos ? std::string::npos : e - ls);
			return l.size() > 160 ? l.substr(0, 160) + "..." : l;
		};
		return "was \"" + line(oldKey) + "\", now \"" + line(newKey) + "\"";
	}

	// Splits a manifest into key text and artifact line. False if malformed.
	inline bool ParseManifest(const std::string& m, std::string& key, uint64_t& size, uint64_t& hash)
	{
		size_t a = m.rfind("\nartifact ");
		if (a == std::string::npos || m.compare(0, strlen(FORMAT), FORMAT) != 0) return false;
		key = m.substr(0, a + 1);
		unsigned long long s = 0, h = 0;
		if (sscanf(m.c_str() + a + 1, "artifact %llu %llx", &s, &h) != 2) return false;
		size = s; hash = h;
		return true;
	}

	// Cache hit: the artifact in a buffer from alloc (size + 1, null-terminated like ReadEntireFile).
	// Miss: nullptr, and why in reason.
	inline void* Load(const Inputs& in, const std::string& key, const std::string& manifest, AllocFn alloc,
		unsigned int* out_size, std::string& reason)
	{
		std::string oldKey;
		uint64_t size = 0, hash = 0;
		if (manifest.empty()) { reason = "no cache yet"; return nullptr; }
		if (!ParseManifest(manifest, oldKey, size, hash)) { reason = "manifest unreadable"; return nullptr; }
		if (oldKey != key) { reason = "inputs changed: " + FirstDifference(oldKey, key); return nullptr; }
		if (size == 0 || size > 0xFFFFFFF0ull) { reason = "bad artifact size"; return nullptr; }

		std::wstring path = CacheDir(in) + L"\\data.win";
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (h == INVALID_HANDLE_VALUE) { reason = "artifact missing"; return nullptr; }
		LARGE_INTEGER fs;
		if (!GetFileSizeEx(h, &fs) || (uint64_t)fs.QuadPart != size) {
			CloseHandle(h);
			reason = "artifact size mismatch (partial write?)";
			return nullptr;
		}
		unsigned char* buf = (unsigned char*)alloc((size_t)size + 1);
		if (!buf) { CloseHandle(h); reason = "allocation failed"; return nullptr; }
		uint64_t got = 0;
		modloader::Xxh64 x;
		while (got < size) {
			DWORD want = (DWORD)std::min<uint64_t>(size - got, 16u << 20), n = 0;
			if (!ReadFile(h, buf + got, want, &n, nullptr) || n == 0) break;
			x.Update(buf + got, n);
			got += n;
		}
		CloseHandle(h);
		if (got != size || x.Digest() != hash) {
			// buf came from the runner's allocator; leaking it once on a corrupt cache is harmless.
			reason = "artifact corrupt (hash mismatch)";
			return nullptr;
		}
		buf[size] = 0;
		if (out_size) *out_size = (unsigned int)size;
		return buf;
	}

	inline bool WriteAll(const std::wstring& path, const void* data, uint64_t len)
	{
		HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) return false;
		const unsigned char* p = (const unsigned char*)data;
		uint64_t done = 0;
		bool ok = true;
		while (ok && done < len) {
			DWORD n = 0, want = (DWORD)std::min<uint64_t>(len - done, 16u << 20);
			ok = WriteFile(h, p + done, want, &n, nullptr) && n == want;
			done += n;
		}
		ok = ok && FlushFileBuffers(h);
		CloseHandle(h);
		if (!ok) DeleteFileW(path.c_str());
		return ok;
	}

	// temp file + rename, so readers only ever see a complete file.
	inline bool WriteAtomic(const std::wstring& path, const void* data, uint64_t len)
	{
		wchar_t suffix[48];
		_snwprintf(suffix, 48, L".tmp%lu", GetCurrentProcessId());
		std::wstring tmp = path + suffix;
		if (!WriteAll(tmp, data, len)) return false;
		if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			DeleteFileW(tmp.c_str());
			return false;
		}
		return true;
	}

	// Stores a fresh compile. The old manifest goes first and the new one last, so a crash in
	// between leaves no manifest (a miss) rather than a manifest that names the wrong artifact.
	// Leftover temp files from a killed writer are removed first.
	inline bool Store(const Inputs& in, const std::string& key, const void* data, uint64_t len)
	{
		std::wstring dir = in.modsDir + L"\\cache";
		CreateDirectoryW(dir.c_str(), nullptr);
		dir += L"\\loosegml";
		CreateDirectoryW(dir.c_str(), nullptr);
		dir = CacheDir(in);
		CreateDirectoryW(dir.c_str(), nullptr);
		std::wstring manifest = dir + L"\\manifest.txt";
		DeleteFileW(manifest.c_str());
		std::vector<std::wstring> stale;
		ListFiles(dir, L"", [](const wchar_t* n) { return wcsstr(n, L".tmp") != nullptr; }, stale, 0);
		for (auto& f : stale) DeleteFileW((dir + L"\\" + f).c_str());
		if (!WriteAtomic(dir + L"\\data.win", data, len)) return false;
		std::string m = key + "artifact " + std::to_string(len) + " " + Hex(modloader::Xxh64::Hash(data, (size_t)len)) + "\n";
		return WriteAtomic(manifest, m.data(), m.size());
	}

	// Store() on a private copy of data, from a background thread: writing ~160 MB (and flushing
	// it) takes seconds under Wine, and the game should not wait for it. The current manifest is
	// deleted before returning, so a launch that starts while the write is still running (or after
	// the game was closed mid-write) is a miss, never a hit on stale data.
	inline bool StoreAsync(const Inputs& in, const std::string& key, const void* data, uint64_t len, LogFn log)
	{
		struct Job { Inputs in; std::string key; void* copy; uint64_t len; LogFn log; };
		void* copy = VirtualAlloc(nullptr, (SIZE_T)len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!copy) return false;
		memcpy(copy, data, (size_t)len);
		DeleteFileW((CacheDir(in) + L"\\manifest.txt").c_str());
		Job* job = new Job{ in, key, copy, len, log };
		HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
			Job* j = (Job*)p;
			DWORD t0 = GetTickCount();
			std::string rel = U8(j->in.relDir);
			if (Store(j->in, j->key, j->copy, j->len))
				j->log("loose GML: cache stored for %s in %lu ms (background)", rel.c_str(), GetTickCount() - t0);
			else
				j->log("loose GML: cache write FAILED for %s (%lu); the next launch compiles again", rel.c_str(), GetLastError());
			VirtualFree(j->copy, 0, MEM_RELEASE);
			delete j;
			return 0;
		}, job, 0, nullptr);
		if (!t) { VirtualFree(copy, 0, MEM_RELEASE); delete job; return false; }
		CloseHandle(t);
		return true;
	}
}
