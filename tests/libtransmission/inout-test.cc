// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <future>
#include <memory>
#include <utility>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/file.h>
#include <libtransmission/inout.h>
#include <libtransmission/torrent.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

namespace libtransmission::test
{

using InOutTest = SessionTest;

TEST_F(InOutTest, writeFailsWhenExistingFileCannotBeOpened)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    auto constexpr MaxWaitMsec = 5000;

    // make the torrent's first file unopenable by replacing it with a directory
    auto const path = tr_torrentFindFile(tor, 0U);
    ASSERT_FALSE(std::empty(path));
    ASSERT_TRUE(tr_sys_path_remove(path.c_str()));
    ASSERT_TRUE(tr_sys_dir_create(path.c_str(), 0, 0700));

    // The write must fail: reporting success would make the caller
    // discard the data, leaving pieces that later fail verification.
    // (Run in the session thread; drop the file descriptors that verify
    // left open first, so the write has to reopen the unopenable path.)
    auto const promise = std::make_shared<std::promise<std::pair<int, tr_stat_errtype>>>();
    auto future = promise->get_future();
    session_->run_in_session_thread(
        [session = session_, tor, promise]()
        {
            session->openFiles().close_torrent(tor->id());
            auto const buf = std::vector<uint8_t>(tr_block_info::BlockSize);
            auto const err = tr_ioWrite(*tor, tor->block_loc(0U), std::size(buf), std::data(buf));
            promise->set_value({ err, tor->error().error_type() });
        });
    ASSERT_EQ(std::future_status::ready, future.wait_for(std::chrono::milliseconds{ MaxWaitMsec }));

    auto const [err, error_type] = future.get();
    EXPECT_NE(0, err);
    EXPECT_EQ(TR_STAT_LOCAL_ERROR, error_type);
}

} // namespace libtransmission::test
