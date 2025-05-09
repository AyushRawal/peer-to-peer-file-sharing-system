# P2P File Sharing System

## Overview

This project implements a peer-to-peer file sharing system similar to BitTorrent. It allows users to share and download files in a distributed manner through a tracker-based architecture. The system divides files into pieces and enables parallel downloading from multiple peers, optimizing transfer speeds and network resource utilization.

## Components

### 1. Tracker
- Acts as a central server that keeps track of files and peers
- Manages user authentication, group memberships, and file metadata
- Helps peers find each other when downloading files
- Implements rarest-piece-first algorithm to optimize download efficiency
- Maintains file piece distribution information

### 2. Client
- Connects to the tracker to register, join groups, and share files
- Splits files into pieces and calculates SHA1 hashes for each piece
- Downloads file pieces from other peers in parallel
- Serves file pieces to other peers upon request
- Implements piece selection strategy based on rarest-piece-first algorithm

### 3. Common Utils
- Provides networking and utility functions shared between tracker and client
- Implements socket communication, message passing, and file handling

## Features

- **User Management**: Registration, login, and authentication
- **Group Management**: Create groups, join groups, leave groups, and list groups
- **File Sharing**: Upload files, download files, and stop sharing
- **Piece Selection**: Implements rarest-piece-first for efficient downloads
- **Parallel Downloading**: Download different pieces from different peers simultaneously
- **Fault Tolerance**: Multiple tracker support for redundancy
- **SHA1 Hashing**: Ensures file integrity during transfers

## Requirements

- Linux operating system
- C++17 compatible compiler
- OpenSSL library for cryptographic functions
- Network connectivity between peers

## Building the Project

Use the provided build script to compile both the tracker and client:

```bash
./build.sh
```

This will generate:
- `tracker.out` - The tracker executable
- `client.out` - The client executable

## Usage

### Setup

1. Create a tracker information file (e.g., `tracker_info.txt`) with IP:Port for each tracker:
   ```
   127.0.0.1:6969
   127.0.0.1:42069
   ```

### Starting Tracker

```bash
./tracker.out <tracker_info_file_path> <tracker_number>
```
Example:
```bash
./tracker.out tracker_info.txt 1
```

### Starting Client

```bash
./client.out <CLIENT_IP:PORT> <tracker_info_file_path>
```
Example:
```bash
./client.out 127.0.0.1:8888 tracker_info.txt
```

## Client Commands

Once the client is running, you can use the following commands:

- **create_user**: `create_user <user_id> <password>`
- **login**: `login <user_id> <password>`
- **create_group**: `create_group <group_id>`
- **join_group**: `join_group <group_id>`
- **leave_group**: `leave_group <group_id>`
- **list_requests**: `list_requests <group_id>`
- **accept_request**: `accept_request <group_id> <user_id>`
- **list_groups**: `list_groups`
- **upload_file**: `upload_file <file_path> <group_id>`
- **list_files**: `list_files <group_id>`
- **download_file**: `download_file <group_id> <file_name> <destination_path>`
- **stop_share**: `stop_share <group_id> <file_name>`
- **logout**: `logout`
- **quit**: `quit` (terminates client)

## System Architecture

The system follows a hybrid peer-to-peer architecture:
- **Centralized Coordination**: Trackers maintain metadata and facilitate peer discovery
- **Distributed Data Transfer**: Actual file transfers occur directly between peers
- **Piece-Based Sharing**: Files are split into 512KB pieces for efficient parallel transfers
- **Multiple Trackers**: Support for multiple trackers provides redundancy

## Implementation Details

- File pieces are fixed at 512KB size
- SHA1 hashing is used for file and piece integrity verification
- Socket programming is used for network communication
- Multithreading is implemented for parallel downloads and client handling
- Error handling for network failures and peer disconnections

## File Structure

```
├── build.sh               # Build script
├── client/                # Client implementation
│   └── client.cpp         # Main client code
├── common/                # Shared utilities
│   ├── utils.cpp          # Common utility implementation
│   └── utils.hpp          # Common utility header
├── tracker/               # Tracker implementation
│   └── tracker.cpp        # Main tracker code
└── tracker_info.txt       # Tracker configuration
```
