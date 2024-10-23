#!/usr/bin/env bash

set -euo pipefail

die()
{
    echo 1>&2 "$@"
    exit 1
}

# return URL for mailboxes for previous, current and next month. This way, we
# don't miss any email, even with different timezones.
mailbox_archives()
{
    current_date=$1

    current_month=$(date +%m --date "$current_date")
    current_year=$(date +%Y --date "$current_date")

    if [ $current_month == "01" ]; then
        previous_month=12
        previous_year=$((current_year - 1))
    elif [ $current_month == "12" ]; then
        next_month=01
        next_year=$((current_year + 1))
    else
        previous_year=$current_year
        next_year=$current_year
        previous_month=$(printf "%02d" $((current_month - 1)))
        next_month=$(printf "%02d" $((current_month + 1)))
    fi

    qemu_archive="https://lists.gnu.org/archive/mbox/qemu-devel/"
    echo $qemu_archive$previous_year-$previous_month \
         $qemu_archive$current_year-$current_month \
         $qemu_archive$next_year-$next_month
}

# download all emails for previous, current and next month.
fetch_mails()
{
    out=$1
    rm -rf $out
    mkdir -p $out
    mkdir -p $out.mailbox
    pushd $out.mailbox
    archives=$(mailbox_archives "$(date)")
    # we can have missing current or next mailbox depending on timezone
    wget --no-verbose --no-clobber $archives || true
    popd
    git mailsplit -o$out $out.mailbox/*
}

find_series()
{
    mail_dir=$1
    # find all message id, for mails without a reference (i.e. first in series).
    grep -Lri '^References: .' $mail_dir | sort | while read m
    do
        # skip messages replying to thread
        if grep -qi '^In-Reply-to: .' $m; then
            continue
        fi
        # skip messages whose subject does not start with [
        if ! grep -q 'Subject: \[' $m; then
            continue
        fi
        msg_id=$(grep -i '^message-id: ' $m | head -n1 |
                 sed -e 's/.*<//' -e 's/>$//')
        date=$(grep -i '^date: ' $m | head -n1 | sed -e 's/^date: //I')
        echo "$msg_id|$date"
    done
}

fetch_repositories()
{
    git remote remove upstream || true
    git remote add upstream -f https://gitlab.com/qemu-project/qemu
    git fetch -a origin -p
}

push_one_series()
{
    s="$1"; shift
    apply_revision="$1"; shift
    echo "-----------------------------------------------------------------"
    msg_id=$(echo "$s" | cut -f 1 -d '|')
    date=$(echo "$s" | cut -f 2 -d '|')
    echo "$msg_id | $date"
    if git rev-parse "remotes/origin/$msg_id" >& /dev/null; then
        return
    fi

    base=$(git rev-parse HEAD)

    # find git commit on master close to date of series
    base_git_revision=$(git log -n1 --format=format:%H --before="$date" \
                        --first-parent upstream/master)
    echo "push this new series, applied from $base_git_revision"
    git checkout "$base_git_revision" >& /dev/null
    git branch -D new_series >& /dev/null || true
    git checkout -b new_series

    # apply series
    if ! b4 shazam --allow-unicode-control-chars $msg_id |& tee shazam.log; then
        git am --abort
        git add shazam.log
        git commit -m 'b4 shazam failed'
    fi

    if ! grep -qi 'no patches found' shazam.log; then
        git push --set-upstream origin "new_series:$msg_id"
        # apply a specific patch
        if [ "$apply_revision" != "" ]; then
            git cherry-pick "$apply_revision" --no-commit
            git commit -a -m 'ci patch' --signoff
        fi
        git push --set-upstream origin "new_series:ci/$msg_id"
    else
        echo "no patches found in series: $msg_id"
    fi

    # reset branch
    git checkout $base
    git branch -D new_series
}

fetch_repositories
apply_range=$(git merge-base origin/ci upstream/master)
[ "$apply_range" != "" ] || die "can't find revisions to apply to series"
# apply all commits on ci branch
apply_range=$apply_range..origin/ci

fetch_mails mails
find_series mails | while read s; do push_one_series "$s" "$apply_range"; done
