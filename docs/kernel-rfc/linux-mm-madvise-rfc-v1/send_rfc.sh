#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# send_rfc.sh — send the MADV_KV_* RFC patch series to linux-mm
#
# Prerequisites:
#   git send-email configured with your SMTP (see ~/.gitconfig)
#   All 5 patch files present in the same directory as this script
#
# Usage:
#   chmod +x send_rfc.sh
#   ./send_rfc.sh           # dry run (--dry-run)
#   ./send_rfc.sh --send    # actually send
#
# Your ~/.gitconfig needs:
#   [sendemail]
#       smtpServer   = smtp.gmail.com
#       smtpServerPort = 587
#       smtpEncryption = tls
#       smtpUser     = mlachwani@gmail.com
#       from         = Manish Keshav Lachwani <mlachwani@gmail.com>
#       confirm      = always
#       chainreplyto = true    # keeps thread structure

set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SEND_FLAGS="--dry-run"
if [[ "$1" == "--send" ]]; then
    SEND_FLAGS=""
    echo ">>> LIVE SEND — patches will be delivered to linux-mm@kvack.org"
else
    echo ">>> DRY RUN — pass --send to actually deliver"
fi

# Primary list and key reviewers
TO="linux-mm@kvack.org"
CC=(
    "Andrew Morton <akpm@linux-foundation.org>"
    "Lorenzo Stoakes <lorenzo.stoakes@oracle.com>"
    "Vlastimil Babka <vbabka@suse.cz>"
    "David Hildenbrand <david@redhat.com>"
    "Michal Hocko <mhocko@suse.com>"
    "Johannes Weiner <hannes@cmpxchg.org>"
    "SeongJae Park <sj@kernel.org>"
    "Alistair Popple <apopple@nvidia.com>"
    "linux-api@vger.kernel.org"
    "linux-kernel@vger.kernel.org"
)

CC_FLAGS=()
for addr in "${CC[@]}"; do
    CC_FLAGS+=(--cc "$addr")
done

PATCHES=(
    "$DIR/0000-cover-letter.patch"
    "$DIR/0001-mm-madvise-add-kv-handler-registration.patch"
    "$DIR/0002-uapi-add-MADV-KV-constants.patch"
    "$DIR/0003-mm-madvise-implement-kv-handlers.patch"
    "$DIR/0004-selftests-mm-add-kv-madvise-tests.patch"
    "$DIR/0004b-mm-kconfig-kv-madvise.patch"
)

# Verify all patches exist
for p in "${PATCHES[@]}"; do
    if [[ ! -f "$p" ]]; then
        echo "ERROR: patch not found: $p"
        exit 1
    fi
done

echo ""
echo "Patches to send:"
for p in "${PATCHES[@]}"; do
    echo "  $(basename $p)"
done
echo ""
echo "To:  $TO"
echo "Cc:  ${CC[*]}"
echo ""

git send-email \
    $SEND_FLAGS \
    --to="$TO" \
    "${CC_FLAGS[@]}" \
    --suppress-cc=all \
    --no-signed-off-by-cc \
    --thread \
    --cover-letter \
    "${PATCHES[@]}"

echo ""
echo "Done."
echo ""
echo "After sending, monitor responses at:"
echo "  https://lore.kernel.org/linux-mm/"
echo ""
echo "Key people to watch for replies from:"
echo "  akpm@linux-foundation.org  (Andrew Morton — mm tree maintainer)"
echo "  lorenzo.stoakes@oracle.com (Lorenzo Stoakes — recent madvise changes)"
echo "  vbabka@suse.cz             (Vlastimil Babka — mm reviewer)"
echo "  david@redhat.com           (David Hildenbrand — mm reviewer)"
