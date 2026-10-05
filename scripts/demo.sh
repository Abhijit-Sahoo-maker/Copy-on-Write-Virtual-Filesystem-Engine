#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SHELL_BIN="$ROOT_DIR/build/cowfs-shell"

if [ ! -f "$SHELL_BIN" ]; then
    echo "Building CowFS first..."
    make -C "$ROOT_DIR" build
fi

DISK_IMG="$ROOT_DIR/demo_disk.img"
rm -f "$DISK_IMG"

echo "=========================================================="
echo "    COWFS LIVE ARCHITECTURAL & INTERACTIVE DEMO           "
echo "=========================================================="

"$SHELL_BIN" << 'EOF'
mkfs demo_disk.img 16
mount demo_disk.img
mkdir /documents
mkdir /system
write /documents/report.txt "Confidential Architecture Report 2026: CoW Subsystem."
cat /documents/report.txt
stat /documents/report.txt
blocks /documents/report.txt

cp /documents/report.txt /documents/report_clone.txt
ls /documents
blocks /documents/report_clone.txt

write /documents/report_clone.txt "Modified Branch: Copy-on-Write split occurred!"
blocks /documents/report.txt
blocks /documents/report_clone.txt

snap create baseline_v1
snap list
append /documents/report.txt " Extra paragraph added after baseline snapshot."
cat /documents/report.txt
snap restore baseline_v1
cat /documents/report.txt

dedup
cache
io
status
sync
unmount
exit
EOF

rm -f "$DISK_IMG"
echo "=========================================================="
echo "Demo finished successfully!"
echo "=========================================================="
