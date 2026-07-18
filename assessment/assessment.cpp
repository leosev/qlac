/* QLAC — a low-latency lossless audio codec
 *
 * This file is original QLAC code and is not derived from FLAC.
 *
 * Copyright (C) 2025  Leonardo Severi
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <QLAC++/block_codec.hpp>

#include <argparse/argparse.hpp>
#include <sndfile.h>

#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <pthread.h>
#include <sched.h>
namespace run {
	std::vector<std::vector<int32_t>> wav_content;
	SF_INFO info { };
	std::vector<unsigned> blocksizes;
	unsigned compression_level;
	std::string apodization;
	std::vector<std::vector<std::vector<qlac::block_info>>> block_infos; // level 1 : channels, level 2: block sizes
	std::vector<std::byte> frame_buf(4096 * (sizeof(std::int32_t) + 1)); // assume max 4096

} // namespace
static void read_samples(SNDFILE *snd);
static void parse_blocksizes(std::string &blocksizes);
static void run_thread();
static void write_meta(std::string &output_meta_fname, const std::string &output_csv_fname);
static const char *solve_type(QLAC__FrameType ft);
static inline auto now()
{
	return std::chrono::steady_clock::now();
}
template <typename T>
static inline int32_t to_nano(const T &t)
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(t).count();
}
static inline int get_bit_depth()
{
	switch(run::info.format & SF_FORMAT_SUBMASK) {
		case SF_FORMAT_PCM_S8:
		case SF_FORMAT_PCM_U8:
			return 8;
			break;
		case SF_FORMAT_PCM_16:
			return 16;
		case SF_FORMAT_PCM_24:
			return 24;
		case SF_FORMAT_PCM_32:
			return 32;
		default:
			return 16;
	}
}
template <typename T>
static bool span_equal(std::span<T> a, std::span<T> b)
{
	if(a.size() != b.size())
		return false;
	for(size_t i = 0; i < a.size(); ++i) {
		if(a[i] != b[i])
			return false;
	}
	return true;
}
static void *t_entry(void *)
{
	for(size_t ch = 0; ch < run::info.channels; ++ch) {
		auto &channel = run::wav_content[ch];
		auto &ch_binfos = run::block_infos[ch];
		for(size_t bs = 0; bs < run::blocksizes.size(); ++bs) {
			auto &bs_binfos = ch_binfos[bs];
			qlac::Config conf {
				.bits_per_sample = get_bit_depth(),
				.blocksize = run::blocksizes[bs]
			};
			qlac::Encoder enc(conf, run::compression_level, run::apodization == "none"? "rectangle" : "default");
			qlac::Decoder dec(conf);
			auto &in = enc.input_samples();
			size_t blocks = run::info.frames / conf.blocksize; // discard remainder
			for(size_t b = 0; b < blocks; ++b) {
				auto start_it = channel.begin() + b * conf.blocksize;
				std::span<int32_t> block(start_it, conf.blocksize);
				for(size_t s = 0; s < conf.blocksize; ++s)
					in[s] = block[s];
				size_t comp_size;
				auto start = now();
				auto encoded = enc.encode(run::frame_buf);
				comp_size = encoded.size_bytes();
				auto after_enc = now();
				auto decoded = dec.decode(encoded);
				auto after_dec = now();
				auto &binfo = bs_binfos.emplace_back(enc.create_block_info());
				binfo.compressed_size = comp_size;
				binfo.encoding_time_ns =
					to_nano(after_enc - start);
				binfo.decoding_time_ns = to_nano(after_dec - after_enc);
				#ifdef DO_VERIFY
				if(!span_equal(decoded,block)){
					throw qlac::Error("Not equal frames, aborting");
				}
				#endif 
			}
		}
	}
	return nullptr;
}
int main(int argc, char *argv[])
{
	argparse::ArgumentParser program("Benchmark QLAC");
	program.add_argument("wav")
		.help("path to a WAV input file");
	program.add_argument("blocksizes")
		.help("comma-separated values of samples per encoded block");
	program.add_argument("output_csv")
		.help("Output csv file");
	program.add_argument("output_meta")
		.help("Output meta file");
	program.add_argument("--apodization", "-a")
		.choices("default", "none")
		.default_value("default")
		.help("Switch between encoder default apodization and none (rectangular)");
	program.add_argument("--compression-level", "-c")
		.scan<'u', unsigned>()
		.default_value(12u)
		.help("FLAC-like compression level");
	program.parse_args(argc, argv);
	auto wav_path = program.get<std::string>("wav");
	auto blocksizes = program.get<std::string>("blocksizes");
	parse_blocksizes(blocksizes);
	run::compression_level = program.get<unsigned>("--compression-level");
	run::apodization = program.get<std::string>("--apodization");
	SNDFILE *snd = sf_open(wav_path.c_str(), SFM_READ, &run::info);
	if(snd == nullptr) {
		throw qlac::Error("Bad wav file");
	}
	auto output_csv_fname = program.get<std::string>("output_csv");
	auto output_meta_fname = program.get<std::string>("output_meta");
	std::ofstream output_csv_file(output_csv_fname);
	output_csv_file << "block,size (samples),channel,encode_time,decode_time,encode_type,encoded_size,order,rice_b,rice_raw,wasted_bits" << std::endl;
	std::cerr << "Writing metadata" << std::endl;
	write_meta(output_meta_fname, output_csv_fname);
	std::cerr << "✅ done!" << std::endl
			  << "Reading file..." << std::endl;
	read_samples(snd);
	sf_close(snd);
	std::cerr << "✅ done!" << std::endl;

	run::block_infos.resize(run::info.channels);
	for(auto &binfoc : run::block_infos) {
		binfoc.resize(run::blocksizes.size());
	}
	run_thread();
	std::cerr << "Writing output" << std::endl;
	for(size_t ch = 0; ch < run::block_infos.size(); ++ch) {
		for(size_t bs = 0; bs < run::blocksizes.size(); ++bs) {
			auto &entry_array = run::block_infos[ch][bs];
			for(size_t bn = 0; bn < entry_array.size(); ++bn) {
				auto &entry = entry_array[bn];
				unsigned order = 0, rice_b = 0, rice_raw = 0, wasted_bits = entry.wasted_bits;
				bool is_lpc_or_fixed = entry.type == QLAC__FRAME_TYPE_LPC || entry.type == QLAC__FRAME_TYPE_FIXED;
				if(is_lpc_or_fixed) {
					order = entry.order;
				}
				if(is_lpc_or_fixed || entry.type == QLAC__FRAME_TYPE_VERBATIM) {
					if(entry.rice_parameter < 31)
						rice_b = entry.rice_parameter;
					else {
						rice_raw = entry.rice_raw_bits;
					}
				}
				output_csv_file << bn << ','
								<< run::blocksizes[bs] << ','
								<< ch << ','
								<< entry.encoding_time_ns << ','
								<< entry.decoding_time_ns << ','
								<< solve_type(entry.type) << ','
								<< entry.compressed_size << ','
								<< order << ','
								<< rice_b << ','
								<< rice_raw << ','
								<< wasted_bits << std::endl;
			}
		}
	}
	std::cerr << "✅ done!" << std::endl;
}
static const char *solve_type(QLAC__FrameType ft)
{
	switch(ft) {
		case QLAC__FRAME_TYPE_CONSTANT:
			return "const";
		case QLAC__FRAME_TYPE_FIXED:
			return "fixed";
		case QLAC__FRAME_TYPE_VERBATIM:
			return "verbatim";
		case QLAC__FRAME_TYPE_LPC:
			return "lpc";
		default:
			throw qlac::Error("Bad frame type");
	}
}
static void run_thread()
{
	std::cerr << "Starting simulation..." << std::endl;
	pthread_t thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	struct sched_param sp { };
	sp.sched_priority = sched_get_priority_min(SCHED_FIFO);
	pthread_attr_setschedparam(&attr, &sp);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_create(&thread, &attr, t_entry, nullptr);
	pthread_attr_destroy(&attr);
	pthread_join(thread, nullptr);
	std::cerr << "✅ done!" << std::endl;
}
static void parse_blocksizes(std::string &blocksizes)
{
	{
		std::stringstream ss(blocksizes);
		std::string token;
		while(std::getline(ss, token, ',')) {
			token.erase(0, token.find_first_not_of(" \t\r\n"));
			token.erase(token.find_last_not_of(" \t\r\n") + 1);
			if(!token.empty()) {
				unsigned bs = static_cast<unsigned>(std::stoul(token));
				if(bs < 16 || bs > 4096)
					throw std::runtime_error("blocksize must be in 16..4096 (got " + token + ")");
				run::blocksizes.push_back(bs);
			}
		}
	}
}
static void write_meta(std::string &output_meta_fname, const std::string &output_csv_fname)
{
	{
		std::ofstream output_meta_file(output_meta_fname);
		output_meta_file << "{\n"
						 << "    \"csv\": \"" << output_csv_fname << "\",\n"
						 << "    \"channels\": " << run::info.channels << ",\n"
						 << "    \"bitdepth\": " << get_bit_depth() << ",\n"
						 << "    \"compression_level\": " << run::compression_level << ",\n"
						 << "    \"apodization\": \"" << run::apodization << "\",\n"
						 << "    \"original_size\": " << run::info.frames << "\n"
						 << "}\n";
	}
}
static void read_samples(SNDFILE *snd)
{
	run::wav_content.resize(run::info.channels);
	for(auto &wc : run::wav_content) {
		wc.resize(run::info.frames);
	}
	if(run::info.channels > 1) {
		size_t read = 0;
		constexpr size_t max_linear_read = 1U << 24;
		std::vector<int32_t> buffer(max_linear_read * run::info.channels);
		for(;;) {
			sf_count_t read_frames = sf_readf_int(snd, buffer.data(), max_linear_read);
			if(read_frames <= 0)
				break;
			for(sf_count_t f = 0; f < read_frames; ++f)
				for(int ch = 0; ch < run::info.channels; ++ch)
					run::wav_content[ch][read + f] = buffer[f * run::info.channels + ch];
			read += read_frames;
		}
	}
	else {
		sf_readf_int(snd, run::wav_content[0].data(), run::info.frames);
	}
	const int shift = 32 - get_bit_depth();
	if(shift > 0) {
		for(auto &w : run::wav_content)
			for(int32_t &s : w)
				s >>= shift;
	}
}
