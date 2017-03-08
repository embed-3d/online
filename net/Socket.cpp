/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <ctype.h>

#include "SigUtil.hpp"
#include "Socket.hpp"
#include "ServerSocket.hpp"

// help with initialization order
namespace {
    std::vector<int> &getWakeupsArray()
    {
        static std::vector<int> pollWakeups;
        return pollWakeups;
    }
    std::mutex &getPollWakeupsMutex()
    {
        static std::mutex pollWakeupsMutex;
        return pollWakeupsMutex;
    }
}

SocketPoll::SocketPoll(const std::string& threadName) :
    _name(threadName),
    _stop(false)
{
    // Create the wakeup fd.
    if (::pipe2(_wakeup, O_CLOEXEC | O_NONBLOCK) == -1)
    {
        throw std::runtime_error("Failed to allocate pipe for SocketPoll waking.");
    }

    {
        std::lock_guard<std::mutex> lock(getPollWakeupsMutex());
        getWakeupsArray().push_back(_wakeup[1]);
    }

    _thread = std::thread(&SocketPoll::pollingThread, this);
}

SocketPoll::~SocketPoll()
{
    stop();
    if (_thread.joinable())
    {
        _thread.join();
    }

    ::close(_wakeup[0]);
    ::close(_wakeup[1]);

    std::lock_guard<std::mutex> lock(getPollWakeupsMutex());
    auto it = std::find(getWakeupsArray().begin(),
                        getWakeupsArray().end(),
                        _wakeup[1]);

    if (it != getWakeupsArray().end())
        getWakeupsArray().erase(it);
}

void SocketPoll::wakeupWorld()
{
    for (const auto& fd : getWakeupsArray())
        wakeup(fd);
}

void ServerSocket::dumpState()
{
    std::cerr << "\t" << getFD() << "\t<accept>\n";
}

namespace {

void dump_hex (const char *legend, const char *prefix, std::vector<char> buffer)
{
    unsigned int i, j;
    fprintf (stderr, "%s", legend);
    for (j = 0; j < buffer.size() + 15; j += 16)
    {
        fprintf (stderr, "%s0x%.4x  ", prefix, j);
        for (i = 0; i < 16; i++)
        {
            if ((j + i) < buffer.size())
                fprintf (stderr, "%.2x ", (unsigned char)buffer[j+i]);
            else
                fprintf (stderr, "   ");
            if (i == 8)
                fprintf (stderr, " ");
        }
        fprintf (stderr, " | ");

        for (i = 0; i < 16; i++)
            if ((j + i) < buffer.size() && ::isprint(buffer[j+i]))
                fprintf (stderr, "%c", buffer[j+i]);
            else
                fprintf (stderr, ".");
        fprintf (stderr, "\n");
    }
}

} // namespace

void StreamSocket::dumpState()
{
    std::cerr << "\t" << getFD() << "\t" << getPollEvents() << "\t"
              << _inBuffer.size() << "\t" << _outBuffer.size() << "\t"
              << "\n";
    if (_inBuffer.size() > 0)
        dump_hex("\t\tinBuffer:\n", "\t\t", _inBuffer);
    if (_outBuffer.size() > 0)
        dump_hex("\t\toutBuffer:\n", "\t\t", _inBuffer);
}

void SocketPoll::dumpState()
{
    std::cerr << " Poll [" << _pollSockets.size() << "] - wakeup r: "
              << _wakeup[0] << " w: " << _wakeup[1] << "\n";
    std::cerr << "\tfd\tevents\trsize\twsize\n";
    for (auto &i : _pollSockets)
        i->dumpState();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */