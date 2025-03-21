/*
 * Copyright (C) 2007-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FileHandle.h"

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

namespace WTF::FileSystemImpl {

int64_t FileHandle::read(std::span<uint8_t> data)
{
    if (!m_handle)
        return -1;

    do {
        auto bytesRead = ::read(*m_handle, data.data(), data.size());
        if (bytesRead >= 0)
            return bytesRead;
    } while (errno == EINTR);
    return -1;
}

int64_t FileHandle::write(std::span<const uint8_t> data)
{
    if (!m_handle)
        return -1;

    do {
        auto bytesWritten = ::write(*m_handle, data.data(), data.size());
        if (bytesWritten >= 0)
            return bytesWritten;
    } while (errno == EINTR);
    return -1;
}

bool FileHandle::truncate(int64_t offset)
{
    // ftruncate returns 0 to indicate the success.
    return m_handle && !ftruncate(*m_handle, offset);
}

bool FileHandle::flush()
{
    return m_handle && !fsync(*m_handle);
}

int64_t FileHandle::seek(int64_t offset, FileSeekOrigin origin)
{
    if (!m_handle)
        return -1;

    int whence = SEEK_SET;
    switch (origin) {
    case FileSeekOrigin::Beginning:
        whence = SEEK_SET;
        break;
    case FileSeekOrigin::Current:
        whence = SEEK_CUR;
        break;
    case FileSeekOrigin::End:
        whence = SEEK_END;
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }
    return static_cast<int64_t>(lseek(*m_handle, offset, whence));
}

std::optional<PlatformFileID> FileHandle::id()
{
    if (!m_handle)
        return std::nullopt;

    struct stat fileInfo;
    if (fstat(*m_handle, &fileInfo))
        return std::nullopt;

    return fileInfo.st_ino;
}

void FileHandle::close()
{
    if (!m_handle)
        return;

    unlock();
    ::close(*std::exchange(m_handle, std::nullopt));
}

std::optional<uint64_t> FileHandle::size()
{
    if (!m_handle)
        return std::nullopt;

    struct stat fileInfo;
    if (fstat(*m_handle, &fileInfo))
        return std::nullopt;

    return fileInfo.st_size;
}

bool FileHandle::lock(OptionSet<FileLockMode> lockMode)
{
#if USE(FILE_LOCK)
    if (!m_handle)
        return false;

    static_assert(LOCK_SH == WTF::enumToUnderlyingType(FileLockMode::Shared), "LockSharedEncoding is as expected");
    static_assert(LOCK_EX == WTF::enumToUnderlyingType(FileLockMode::Exclusive), "LockExclusiveEncoding is as expected");
    static_assert(LOCK_NB == WTF::enumToUnderlyingType(FileLockMode::Nonblocking), "LockNonblockingEncoding is as expected");

    RELEASE_ASSERT(!m_isLocked);
    m_isLocked = flock(*m_handle, lockMode.toRaw()) != -1;
    return m_isLocked;
#else
    UNUSED_PARAM(lockMode);
    return false;
#endif
}

bool FileHandle::unlock()
{
#if USE(FILE_LOCK)
    if (!m_handle)
        return false;

    if (std::exchange(m_isLocked, false))
        return flock(*m_handle, LOCK_UN) != -1;
    return false;
#else
    return false;
#endif
}

} // WTF::FileSystemImpl
