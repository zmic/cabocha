// CaboCha -- Yet Another Japanese Dependency Parser
//
//  $Id: selector.cpp 50 2009-05-03 08:25:36Z taku-ku $;
//
//  Copyright(C) 2001-2008 Taku Kudo <taku@chasen.org>
#include <cstring>
#include <strstream>
#include "cabocha.h"
#include "selector.h"
#include "common.h"
#include "scoped_ptr.h"
#include "utils.h"
#include "ucs.h"
#include "selector_pat.h"

namespace CaboCha {
inline const char *get_token(const Token *token, size_t id) {
  if (token->feature_list_size <= id) return 0;
  if (std::strcmp("*", token->feature_list[id]) == 0) return 0;
  return token->feature_list[id];
}

PatternMatcher::PatternMatcher() {}
PatternMatcher::~PatternMatcher() {}

void PatternMatcher::clear() {
  patterns_.clear();
}

bool PatternMatcher::compile(const char *pattern,
                             Iconv *iconv) {
  clear();
  std::string converted(pattern);
  if (iconv) {
    if (!iconv->convert(&converted)) {
      std::cerr << "cannot convert: " << pattern << std::endl;
    }
  }
  const size_t len = converted.size();
  const char *pat = converted.c_str();
  if (len >= 3 && pat[0] == '(' && pat[len-1] == ')') {
    scoped_array<char> buf(new char[BUF_SIZE]);
    CHECK_DIE(len < BUF_SIZE - 3) << "too long parameter";
    std::strncpy(buf.get(), pat + 1, BUF_SIZE);
    buf[len-2] = '\0';
    scoped_array<char *> col(new char *[BUF_SIZE]);
    const size_t n = tokenize(buf.get(), "|", col.get(), BUF_SIZE);
    CHECK_DIE(n < BUF_SIZE) << "too long OR nodes";
    for (size_t i = 0; i < n; ++i) {
      patterns_.push_back(std::string(col[i]));
    }
  } else {
    patterns_.push_back(std::string(pat));
  }

  return !patterns_.empty();
}

const char* PatternMatcher::match(const char *str) const {
  for (size_t i = 0; i < patterns_.size(); ++i) {
    if (patterns_[i] == str) {
      return patterns_[i].c_str();
    }
  }
  return 0;
}

const char* PatternMatcher::prefix_match(const char *str) const {
  const size_t len = strlen(str);
  for (size_t i = 0; i < patterns_.size(); ++i) {
    if (len < patterns_[i].size()) {
      continue;
    }
    if (0 == memcmp(str, patterns_[i].data(),
                    patterns_[i].size())) {
      return patterns_[i].c_str();
    }
  }
  return 0;
}

Selector::Selector() {}
Selector::~Selector() {}

void Selector::close() {}

bool Selector::open(const Param &param) {
  Iconv iconv;
  iconv.open(UTF8, charset());
  CHECK_DIE(pat_ipa_func_.compile(IPA_FUNC_PAT, &iconv));
  CHECK_DIE(pat_ipa_head_.compile(IPA_HEAD_PAT, &iconv));
  CHECK_DIE(pat_juman_func_.compile(JUMAN_FUNC_PAT, &iconv));
  CHECK_DIE(pat_juman_head_.compile(JUMAN_HEAD_PAT, &iconv));
  CHECK_DIE(pat_kutouten_.compile(KUTOUTEN_PAT, &iconv));
  CHECK_DIE(pat_open_bracket_.compile(OPEN_BRACKET_PAT,  &iconv));
  CHECK_DIE(pat_close_bracket_.compile(CLOSE_BRACKET_PAT, &iconv));
  CHECK_DIE(pat_dyn_a_.compile(DYN_A_PAT, &iconv));
  CHECK_DIE(pat_case_.compile(CASE_PAT, &iconv));
  return true;
}

bool Selector::parse(Tree *tree) {
  CHECK_FALSE(tree);
  const size_t size = tree->chunk_size();
  const size_t pos_size = (tree->posset() == IPA) ? 4 : 2;

  for (size_t i = 0; i < size; ++i) {  // for all chunks
    const Chunk *chunk = tree->chunk(i);
    const size_t token_size = chunk->token_pos + chunk->token_size;
    char *buf = tree->alloc(2048);
    std::ostrstream ostrs(buf, 2048);

    // for all tokens
    for (size_t j = chunk->token_pos; j < token_size; ++j) {
      const Token *token = tree->token(j);
      const char *p = 0;
      p = pat_kutouten_.match(token->normalized_surface);
      if (p) {
        ostrs << " G_PUNC:" << p;
        ostrs << " F_PUNC:" << p;
      }

      p = pat_open_bracket_.match(token->normalized_surface);
      if (p) {
        ostrs << " G_OB:" << p;
        ostrs << " F_OB:" << p;
      }

      p = pat_close_bracket_.match(token->normalized_surface);
      if (p) {
        ostrs << " G_CB:" << p;
        ostrs << " F_CB:" << p;
      }
    }

    size_t hid = 0;
    size_t fid = 0;
    findHead(*tree, *chunk, &hid, &fid);

    const Token *htoken = tree->token(hid);
    const Token *ftoken = tree->token(fid);

    const char *hsurface = htoken->normalized_surface;
    const char *fsurface = ftoken->normalized_surface;
    const char *hctype   = get_token(htoken, pos_size);
    const char *hcform   = get_token(htoken, pos_size + 1);
    const char *fctype   = get_token(ftoken, pos_size);
    const char *fcform   = get_token(ftoken, pos_size + 1);

    ostrs << " F_H0:" << hsurface;

    const size_t hsize = _min(pos_size,
                              static_cast<size_t>(htoken->feature_list_size));
    for (size_t k = 0; k < hsize; ++k) {
      if (std::strcmp("*", htoken->feature_list[k]) == 0) break;
      ostrs << " F_H" << k + 1 << ':' << htoken->feature_list[k];
    }
    if (hctype) ostrs << " F_H5:" << hctype;
    if (hcform) ostrs << " F_H6:" << hcform;

    ostrs << " F_F0:" << fsurface;
    const size_t fsize = _min(pos_size,
                              static_cast<size_t>(ftoken->feature_list_size));
    for (size_t k = 0; k < fsize; ++k) {
      if (std::strcmp("*", ftoken->feature_list[k]) == 0) break;
      ostrs << " F_F" << k + 1 << ':' << ftoken->feature_list[k];
    }

    if (fctype) ostrs << " F_F5:" << fctype;
    if (fcform) ostrs << " F_F6:" << fcform;

    std::string output;
    if (pat_dyn_a_.prefix_match(ftoken->feature)) {
      ostrs << " A:" << fsurface;
    } else if (fcform) {
      ostrs << " A:" << fcform;
    } else {
      concat_feature(ftoken, pos_size, &output);
      ostrs << " A:" << output;
    }

    concat_feature(htoken, pos_size, &output);
    ostrs << " B:" << output;

    if (pat_case_.prefix_match(ftoken->feature)) {
      ostrs << " G_CASE:" << fsurface;
    }

    if (i == 0) ostrs << " F_BOS:1";
    if (i == size - 1) ostrs << " F_EOS:1";

    ostrs << std::ends;
    // write to tree
    Chunk *mutable_chunk = tree->mutable_chunk(i);
    mutable_chunk->head_pos = hid - chunk->token_pos;
    mutable_chunk->func_pos = fid - chunk->token_pos;

    const int kFeatureSize = 256;
    scoped_array<char *> feature(new char *[kFeatureSize]);
    const size_t s = tokenize(buf + 1, " ", feature.get(), kFeatureSize);
    mutable_chunk->feature_list_size = static_cast<unsigned char>(s);
    mutable_chunk->feature_list = const_cast<const char **>
        (tree->alloc_char_array(s));
    std::copy(feature.get(), feature.get() + s, mutable_chunk->feature_list);
  }

  tree->set_output_layer(OUTPUT_SELECTION);

  return true;
}

void Selector::findHead(const Tree &tree, const Chunk &chunk,
                        size_t *hid, size_t *fid) const {
  *hid = chunk.token_pos;
  *fid = chunk.token_pos;
  //  size_t hid2 = chunk.token_pos;
  const size_t token_size = chunk.token_pos + chunk.token_size;

  for (size_t j = chunk.token_pos; j < token_size; ++j) {
    const Token *token = tree.token(j);
    if (tree.posset() == IPA) {
      if (pat_ipa_func_.prefix_match(token->feature)) {
        *fid = j;
      } else if (!pat_ipa_head_.prefix_match(token->feature)) {
        *hid = j;
      }
    } else if (tree.posset() == JUMAN) {
      if (!pat_juman_func_.prefix_match(token->feature)) {
        *fid = j;
      }
      if (!pat_juman_head_.prefix_match(token->feature)) {
        *hid = j;
      }
    }
    /*else if (tree.posset() == UNIDIC) {
      if (pat_ipa_func_.prefix_match(token->feature)) {
        *fid = j;
      } else if (!pat_ipa_head_.prefix_match(token->feature)) {
        *hid = j;
      } else if (pat_ipa_head2_.prefix_match(token->feature)) {
        hid2 = j;
      }
    }
    */
  }

  if (tree.posset() == IPA && *hid > *fid) {
    *fid = *hid;
  }

  /*
  if (tree.posset() == UNIDIC) {
    if (*hid > hid2) {
      *hid = hid2;
    } else {
      *fid = _max(hid2, *fid);
    }
    if (*hid > *fid) {
      *fid = *hid;
    }
  }
  */
}
}
