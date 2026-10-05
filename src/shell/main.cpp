#include "cowfs/vfs/vfs.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <cstring>

// ANSI Terminal Colors for a professional developer CLI
namespace Color {
    const char* Reset   = "\033[0m";
    const char* Bold    = "\033[1m";
    const char* Red     = "\033[31m";
    const char* Green   = "\033[32m";
    const char* Yellow  = "\033[33m";
    const char* Blue    = "\033[34m";
    const char* Magenta = "\033[35m";
    const char* Cyan    = "\033[36m";
    const char* Gray    = "\033[90m";
}

void print_banner() {
    std::cout << Color::Cyan << Color::Bold
              << "===================================================================\n"
              << "    COWFS: Copy-on-Write Virtual Filesystem Engine (CLI Shell)     \n"
              << "    Advanced Systems, Computer Architecture & Storage Engine      \n"
              << "===================================================================\n"
              << Color::Reset
              << "Type " << Color::Yellow << "help" << Color::Reset << " to see available commands or "
              << Color::Yellow << "exit" << Color::Reset << " to quit.\n\n";
}

void print_help() {
    std::cout << Color::Bold << "Available Commands:\n" << Color::Reset;
    std::cout << "  " << Color::Green << "mkfs <file> <size_mb>" << Color::Reset << "   Format a new virtual disk image\n";
    std::cout << "  " << Color::Green << "mount <file>" << Color::Reset << "           Mount a disk image (or :memory:)\n";
    std::cout << "  " << Color::Green << "unmount" << Color::Reset << "                Safely flush and unmount the filesystem\n";
    std::cout << "  " << Color::Green << "status | df" << Color::Reset << "            Show storage utilization, blocks, cache hit ratio\n";
    std::cout << "  " << Color::Green << "ls [path]" << Color::Reset << "              List directory entries with metadata\n";
    std::cout << "  " << Color::Green << "mkdir <path>" << Color::Reset << "            Create a new directory\n";
    std::cout << "  " << Color::Green << "rmdir <path>" << Color::Reset << "            Remove an empty directory\n";
    std::cout << "  " << Color::Green << "touch <path>" << Color::Reset << "            Create an empty file\n";
    std::cout << "  " << Color::Green << "write <path> <text>" << Color::Reset << "     Write text payload into file\n";
    std::cout << "  " << Color::Green << "append <path> <text>" << Color::Reset << "    Append text payload to file\n";
    std::cout << "  " << Color::Green << "cat <path>" << Color::Reset << "              Display file contents\n";
    std::cout << "  " << Color::Green << "cp <src> <dst>" << Color::Reset << "          Instantaneous Copy-on-Write zero-copy clone\n";
    std::cout << "  " << Color::Green << "rm <path>" << Color::Reset << "               Remove a file\n";
    std::cout << "  " << Color::Green << "stat <path>" << Color::Reset << "             Display inode, generation, and physical blocks\n";
    std::cout << "  " << Color::Green << "blocks <path>" << Color::Reset << "           Inspect physical block IDs and reference counts\n";
    std::cout << "  " << Color::Green << "snap create <name>" << Color::Reset << "      Create an O(1) point-in-time filesystem snapshot\n";
    std::cout << "  " << Color::Green << "snap list" << Color::Reset << "               List all active snapshots\n";
    std::cout << "  " << Color::Green << "snap restore <name>" << Color::Reset << "     Revert entire filesystem state to a snapshot\n";
    std::cout << "  " << Color::Green << "snap delete <name>" << Color::Reset << "      Delete a snapshot and free unshared blocks\n";
    std::cout << "  " << Color::Green << "cache" << Color::Reset << "                  Display page cache hits, misses, and hit ratio\n";
    std::cout << "  " << Color::Green << "io" << Color::Reset << "                     Display I/O throughput and Write Amplification\n";
    std::cout << "  " << Color::Green << "dedup" << Color::Reset << "                  Run block-level deduplication pass\n";
    std::cout << "  " << Color::Green << "sync" << Color::Reset << "                   Flush dirty page cache to disk\n";
    std::cout << "  " << Color::Green << "help" << Color::Reset << "                   Display this help menu\n";
    std::cout << "  " << Color::Green << "exit" << Color::Reset << "                   Exit shell\n\n";
}

int main(int argc, char* argv[]) {
    cowfs::VFS vfs;
    std::string mounted_target = "";

    print_banner();

    // If disk image passed as CLI argument, auto-mount
    if (argc > 1) {
        std::string target = argv[1];
        if (vfs.mount(target) == 0) {
            mounted_target = target;
            std::cout << Color::Green << "[+] Auto-mounted disk: " << target << Color::Reset << "\n\n";
        } else {
            std::cout << Color::Yellow << "[!] Could not mount: " << target << " (Use mkfs first)\n" << Color::Reset;
        }
    }

    std::string line;
    while (true) {
        if (vfs.is_mounted()) {
            std::cout << Color::Cyan << "cowfs[" << mounted_target << "]> " << Color::Reset;
        } else {
            std::cout << Color::Gray << "cowfs[unmounted]> " << Color::Reset;
        }

        if (!std::getline(std::cin, line)) {
            break;
        }

        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "exit" || cmd == "quit") {
            std::cout << "Exiting CowFS shell. Goodbye!\n";
            break;
        } else if (cmd == "help" || cmd == "?") {
            print_help();
        } else if (cmd == "mkfs") {
            std::string file;
            size_t size_mb = 10;
            ss >> file >> size_mb;
            if (file.empty()) {
                std::cout << Color::Red << "Usage: mkfs <filename> [size_in_mb]\n" << Color::Reset;
                continue;
            }
            uint64_t total_blocks = (size_mb * 1024 * 1024) / cowfs::BLOCK_SIZE;
            if (vfs.mkfs(file, total_blocks) == 0) {
                std::cout << Color::Green << "[+] Formatted filesystem on " << file
                          << " (" << size_mb << " MB, " << total_blocks << " blocks)\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] mkfs failed for: " << file << "\n" << Color::Reset;
            }
        } else if (cmd == "mount") {
            std::string file;
            ss >> file;
            if (file.empty()) {
                std::cout << Color::Red << "Usage: mount <filename>\n" << Color::Reset;
                continue;
            }
            if (vfs.is_mounted()) {
                std::cout << Color::Yellow << "[!] Unmounting currently active filesystem first...\n" << Color::Reset;
                vfs.unmount();
                mounted_target = "";
            }
            if (vfs.mount(file) == 0) {
                mounted_target = file;
                std::cout << Color::Green << "[+] Mounted successfully: " << file << "\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] Failed to mount: " << file << "\n" << Color::Reset;
            }
        } else if (cmd == "unmount" || cmd == "umount") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Yellow << "No filesystem mounted.\n" << Color::Reset;
                continue;
            }
            if (vfs.unmount() == 0) {
                std::cout << Color::Green << "[+] Unmounted: " << mounted_target << "\n" << Color::Reset;
                mounted_target = "";
            } else {
                std::cout << Color::Red << "[-] Unmount error\n" << Color::Reset;
            }
        } else if (cmd == "status" || cmd == "df") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            auto st = vfs.status();
            std::cout << Color::Bold << "--- Filesystem Status ---\n" << Color::Reset;
            std::cout << "  Total Blocks:       " << st.total_blocks << " (" << (st.total_blocks * 4 / 1024) << " MB)\n";
            std::cout << "  Used Blocks:        " << st.used_blocks << " (" << std::fixed << std::setprecision(1) << st.block_utilization_pct << "%)\n";
            std::cout << "  Free Blocks:        " << st.free_blocks << "\n";
            std::cout << "  Total Inodes:       " << st.total_inodes << "\n";
            std::cout << "  Active Generation:  " << st.active_generation << "\n";
            std::cout << "  Active Snapshots:   " << st.snapshot_count << "\n";
            std::cout << "  Page Cache Hit %:   " << std::fixed << std::setprecision(2) << st.cache_hit_ratio << "%\n";
            std::cout << "  Write Amplification: " << std::fixed << std::setprecision(2) << st.write_amplification << "x\n";
        } else if (cmd == "ls") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path = "/";
            ss >> path;
            auto entries = vfs.ls(path);
            std::cout << Color::Bold << "Directory listing of " << path << ":\n" << Color::Reset;
            if (entries.empty()) {
                std::cout << "  (empty directory)\n";
            }
            for (const auto& e : entries) {
                std::string type_str = (e.file_type == static_cast<uint8_t>(cowfs::FileType::Directory)) ? "[DIR] " : "[FILE]";
                std::string color = (e.file_type == static_cast<uint8_t>(cowfs::FileType::Directory)) ? Color::Blue : Color::Green;
                std::cout << "  " << color << type_str << " " << std::left << std::setw(20) << e.get_name()
                          << Color::Reset << " (Inode " << e.inode_id << ")\n";
            }
        } else if (cmd == "mkdir") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            if (path.empty()) {
                std::cout << Color::Red << "Usage: mkdir <path>\n" << Color::Reset;
                continue;
            }
            if (vfs.mkdir(path) == 0) {
                std::cout << Color::Green << "[+] Created directory: " << path << "\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] Failed to create directory: " << path << "\n" << Color::Reset;
            }
        } else if (cmd == "rmdir") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            if (vfs.rmdir(path) == 0) {
                std::cout << Color::Green << "[+] Removed directory: " << path << "\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] Failed to remove directory: " << path << "\n" << Color::Reset;
            }
        } else if (cmd == "touch") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            int fd = vfs.open(path, cowfs::OpenFlags::Create | cowfs::OpenFlags::WriteOnly);
            if (fd >= 0) {
                vfs.close(fd);
                std::cout << Color::Green << "[+] Created file: " << path << "\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] Failed to touch file: " << path << "\n" << Color::Reset;
            }
        } else if (cmd == "write") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            std::string text;
            std::getline(ss, text);
            if (!text.empty() && text[0] == ' ') text.erase(0, 1);

            int fd = vfs.open(path, cowfs::OpenFlags::Create | cowfs::OpenFlags::WriteOnly | cowfs::OpenFlags::Truncate);
            if (fd < 0) {
                std::cout << Color::Red << "[-] Could not open file for writing: " << path << "\n" << Color::Reset;
                continue;
            }
            ssize_t written = vfs.write(fd, text.data(), text.size());
            vfs.close(fd);
            std::cout << Color::Green << "[+] Wrote " << written << " bytes to " << path << "\n" << Color::Reset;
        } else if (cmd == "append") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            std::string text;
            std::getline(ss, text);
            if (!text.empty() && text[0] == ' ') text.erase(0, 1);

            int fd = vfs.open(path, cowfs::OpenFlags::Create | cowfs::OpenFlags::WriteOnly | cowfs::OpenFlags::Append);
            if (fd < 0) {
                std::cout << Color::Red << "[-] Could not open file for append: " << path << "\n" << Color::Reset;
                continue;
            }
            ssize_t written = vfs.write(fd, text.data(), text.size());
            vfs.close(fd);
            std::cout << Color::Green << "[+] Appended " << written << " bytes to " << path << "\n" << Color::Reset;
        } else if (cmd == "cat") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            int fd = vfs.open(path, cowfs::OpenFlags::ReadOnly);
            if (fd < 0) {
                std::cout << Color::Red << "[-] File not found: " << path << "\n" << Color::Reset;
                continue;
            }

            char buffer[1024];
            ssize_t n = 0;
            while ((n = vfs.read(fd, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[n] = '\0';
                std::cout << buffer;
            }
            std::cout << "\n";
            vfs.close(fd);
        } else if (cmd == "cp") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string src, dst;
            ss >> src >> dst;
            if (src.empty() || dst.empty()) {
                std::cout << Color::Red << "Usage: cp <src> <dst>\n" << Color::Reset;
                continue;
            }
            if (vfs.clone(src, dst) == 0) {
                std::cout << Color::Green << "[+] CoW Zero-Copy Clone created: " << src << " -> " << dst
                          << " (0 physical data blocks duplicated!)\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] Failed to clone file: " << src << " -> " << dst << "\n" << Color::Reset;
            }
        } else if (cmd == "rm") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            if (vfs.unlink(path) == 0) {
                std::cout << Color::Green << "[+] Removed: " << path << "\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] Failed to remove: " << path << "\n" << Color::Reset;
            }
        } else if (cmd == "stat") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            cowfs::FileStat st;
            if (vfs.stat(path, &st) == 0) {
                std::cout << Color::Bold << "--- File Stat: " << path << " ---\n" << Color::Reset;
                std::cout << "  Inode ID:          " << st.inode_id << "\n";
                std::cout << "  File Type:         " << (st.type == cowfs::FileType::Directory ? "Directory" : "Regular File") << "\n";
                std::cout << "  Size:              " << st.size << " bytes\n";
                std::cout << "  Blocks Count:      " << st.blocks_count << "\n";
                std::cout << "  Generation (CoW):  " << st.generation << "\n";
                std::cout << "  Allocated Blocks:  [ ";
                for (auto b : st.allocated_blocks) std::cout << b << " ";
                std::cout << "]\n";
            } else {
                std::cout << Color::Red << "[-] Stat failed for: " << path << "\n" << Color::Reset;
            }
        } else if (cmd == "blocks") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string path;
            ss >> path;
            cowfs::FileStat st;
            if (vfs.stat(path, &st) == 0) {
                std::cout << Color::Bold << "Physical Block Mapping & CoW Refcounts for: " << path << "\n" << Color::Reset;
                auto* fs = vfs.get_filesystem();
                for (size_t i = 0; i < st.allocated_blocks.size(); ++i) {
                    cowfs::block_id_t blk = st.allocated_blocks[i];
                    uint16_t ref = fs ? fs->get_block_refcount(blk).value_or(0) : 0;
                    std::cout << "  Logical Block " << i << " -> Physical Block " << blk
                              << " | Reference Count: " << Color::Yellow << ref << Color::Reset
                              << (ref > 1 ? " [SHARED - CoW active]" : " [EXCLUSIVE]") << "\n";
                }
            } else {
                std::cout << Color::Red << "[-] Could not read blocks for: " << path << "\n" << Color::Reset;
            }
        } else if (cmd == "snap") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            std::string subcmd;
            ss >> subcmd;
            if (subcmd == "create") {
                std::string name;
                ss >> name;
                if (name.empty()) {
                    std::cout << Color::Red << "Usage: snap create <snapshot_name>\n" << Color::Reset;
                    continue;
                }
                if (vfs.snapshot_create(name) == 0) {
                    std::cout << Color::Green << "[+] Snapshot '" << name << "' created instantaneously!\n" << Color::Reset;
                } else {
                    std::cout << Color::Red << "[-] Failed to create snapshot: " << name << "\n" << Color::Reset;
                }
            } else if (subcmd == "list") {
                auto list = vfs.snapshot_list();
                std::cout << Color::Bold << "--- Active Snapshots ---\n" << Color::Reset;
                if (list.empty()) std::cout << "  (none)\n";
                for (const auto& s : list) {
                    std::cout << "  Snapshot: " << Color::Magenta << std::left << std::setw(20) << s.name
                              << Color::Reset << " | Gen: " << s.generation
                              << " | Cloned Root Inode: " << s.root_inode_id << "\n";
                }
            } else if (subcmd == "restore") {
                std::string name;
                ss >> name;
                if (name.empty()) {
                    std::cout << Color::Red << "Usage: snap restore <snapshot_name>\n" << Color::Reset;
                    continue;
                }
                if (vfs.snapshot_restore(name) == 0) {
                    std::cout << Color::Green << "[+] Filesystem successfully restored to snapshot: '" << name << "'!\n" << Color::Reset;
                } else {
                    std::cout << Color::Red << "[-] Failed to restore snapshot: " << name << "\n" << Color::Reset;
                }
            } else if (subcmd == "delete") {
                std::string name;
                ss >> name;
                if (vfs.snapshot_delete(name) == 0) {
                    std::cout << Color::Green << "[+] Snapshot '" << name << "' deleted.\n" << Color::Reset;
                } else {
                    std::cout << Color::Red << "[-] Failed to delete snapshot: " << name << "\n" << Color::Reset;
                }
            } else {
                std::cout << Color::Red << "Unknown snapshot command. Use: snap <create|list|restore|delete>\n" << Color::Reset;
            }
        } else if (cmd == "cache") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            const auto* c = vfs.cache_stats();
            if (c) {
                std::cout << Color::Bold << "--- Page Cache / Buffer Pool Statistics ---\n" << Color::Reset;
                std::cout << "  Cache Hits:         " << c->hits.load() << "\n";
                std::cout << "  Cache Misses:       " << c->misses.load() << "\n";
                std::cout << "  Dirty Evictions:    " << c->dirty_evictions.load() << "\n";
                std::cout << "  Total Accesses:     " << c->total_accesses.load() << "\n";
                std::cout << "  Hit Ratio:          " << Color::Green << std::fixed << std::setprecision(2)
                          << c->hit_ratio() << "%\n" << Color::Reset;
            }
        } else if (cmd == "io") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            const auto* io = vfs.io_stats();
            if (io) {
                std::cout << Color::Bold << "--- Hardware Block Device I/O Statistics ---\n" << Color::Reset;
                std::cout << "  Physical Reads:     " << io->reads_count.load() << " blocks (" << (io->bytes_read.load() / 1024) << " KB)\n";
                std::cout << "  Physical Writes:    " << io->writes_count.load() << " blocks (" << (io->bytes_written.load() / 1024) << " KB)\n";
                std::cout << "  Device Syncs:       " << io->syncs_count.load() << "\n";
                std::cout << "  User Payload Written: " << io->user_payload_bytes_written.load() << " bytes\n";
                std::cout << "  Write Amplification Factor: " << Color::Yellow << std::fixed << std::setprecision(2)
                          << io->write_amplification_factor() << "x\n" << Color::Reset;
            }
        } else if (cmd == "dedup") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            size_t merged = vfs.dedup();
            std::cout << Color::Green << "[+] Block deduplication complete! Merged " << merged
                      << " duplicate blocks (" << (merged * cowfs::BLOCK_SIZE / 1024) << " KB saved)\n" << Color::Reset;
        } else if (cmd == "sync") {
            if (!vfs.is_mounted()) {
                std::cout << Color::Red << "Error: Filesystem not mounted.\n" << Color::Reset;
                continue;
            }
            if (vfs.sync() == 0) {
                std::cout << Color::Green << "[+] All dirty pages synced and dual-superblock committed atomically.\n" << Color::Reset;
            } else {
                std::cout << Color::Red << "[-] Sync failed.\n" << Color::Reset;
            }
        } else {
            std::cout << Color::Red << "Unknown command: '" << cmd << "'. Type 'help' for instructions.\n" << Color::Reset;
        }
    }

    return 0;
}
