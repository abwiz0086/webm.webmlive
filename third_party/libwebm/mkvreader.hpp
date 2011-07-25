// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#ifndef MKVREADER_HPP
#define MKVREADER_HPP

#include "mkvparser.hpp"
#include <cstdio>

namespace mkvparser
{

class MkvReader : public IMkvReader
{
    MkvReader(const MkvReader&);
    MkvReader& operator=(const MkvReader&);
public:
    MkvReader();
    virtual ~MkvReader();

    int Open(const char*);
    void Close();
    bool IsOpen() const;

    virtual int Read(long long position, long length, unsigned char* buffer);
    virtual int Length(long long* total, long long* available);
private:
    long long m_length;
    FILE* m_file;
};

}  //end namespace mkvparser

#endif //MKVREADER_HPP
