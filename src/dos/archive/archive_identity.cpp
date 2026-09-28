// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/archive_identity.h"

#include "utils/checks.h"

#include <algorithm>
#include <array>
#include <cstdio>

// sha256.h typedefs BYTE and WORD (as unsigned int), which clash with
// windows.h; this file must never include a Windows header.
extern "C" {
#include "sha256.h"
}

CHECK_NARROWING();

namespace ArchiveIdentity {

std::string Sha256Hex(const std::span<const uint8_t> bytes)
{
	SHA256_CTX ctx = {};
	sha256_init(&ctx);
	sha256_update(&ctx, bytes.data(), bytes.size());
	std::array<uint8_t, SHA256_BLOCK_SIZE> digest = {};
	sha256_final(&ctx, digest.data());

	constexpr auto hex = std::string_view("0123456789abcdef");
	auto out           = std::string();
	out.reserve(digest.size() * 2);
	for (const auto byte : digest) {
		out.push_back(hex[byte >> 4]);
		out.push_back(hex[byte & 0x0f]);
	}
	return out;
}

std::string CanonicalListing(std::vector<ListingEntry> entries,
                             const int64_t archive_length)
{
	std::ranges::sort(entries, {}, &ListingEntry::utf8_path);
	auto out = std::string();
	for (const auto& e : entries) {
		std::array<char, 16> crc = {};
		if (e.has_crc) {
			std::snprintf(crc.data(), crc.size(), "%08x", e.crc32);
		} else {
			crc[0] = '-';
		}
		out += e.utf8_path;
		out += '\t';
		out += std::to_string(e.size);
		out += '\t';
		out += std::to_string(e.mtime);
		out += '\t';
		out += crc.data();
		out += '\n';
	}
	out += "length\t";
	out += std::to_string(archive_length);
	out += '\n';
	return out;
}

std::string Hash(const std::vector<ListingEntry>& entries, const int64_t archive_length)
{
	const auto listing = CanonicalListing(entries, archive_length);
	return Sha256Hex(std::span<const uint8_t>(
	        reinterpret_cast<const uint8_t*>(listing.data()), listing.size()));
}

std::string SanitizeStem(const std::string_view stem)
{
	auto out = std::string();
	for (const auto c : stem) {
		const auto ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		                (c >= '0' && c <= '9') || c == '.' ||
		                c == '_' || c == '-';
		out.push_back(ok ? c : '_');
	}
	if (out.empty() || out == "." || out == "..") {
		return "archive";
	}
	return out;
}

std::string ShortKey(const std::string_view stem, const std::string_view hash_hex)
{
	return SanitizeStem(stem) + "-" + std::string(hash_hex.substr(0, 8));
}

} // namespace ArchiveIdentity
