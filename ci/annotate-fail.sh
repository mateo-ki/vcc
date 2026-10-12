# usage: sh ci/annotate-fail.sh <exit-status> <label> <logfile>
#
# Job logs on GitHub Actions need repository admin rights to download, so a failing step
# quotes the tail of its own log into a check-run annotation. Annotations are readable
# through the public check-runs API, which means the failure reason can be fetched without
# anyone pasting logs by hand.

status="$1"
label="$2"
log="$3"

if [ "$status" -eq 0 ]; then
    exit 0
fi

# Workflow command syntax: one line, % escaped first, then newlines collapsed.
message=$(tail -n 14 "$log" 2>/dev/null | sed 's/%/%25/g' | tr '\r\n' '  ' | cut -c1-900)

printf '::error::%s failed with exit %s: %s\n' "$label" "$status" "$message"
