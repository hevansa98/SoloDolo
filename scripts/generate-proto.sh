#!/usr/bin/env bash
# Regenerates Solo's Swift protobuf code from the shared wire contract.
# Requires: brew install swift-protobuf   (provides protoc and protoc-gen-swift)
# Keep protoc-gen-swift's version in step with the SwiftProtobuf package in Xcode.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
out_dir="$repo_root/SoloDolo/Generated"

mkdir -p "$out_dir"
protoc \
  --proto_path="$repo_root/proto" \
  --swift_out="$out_dir" \
  --swift_opt=Visibility=Internal \
  --swift_opt=FileNaming=DropPath \
  solodolo/v1/solodolo.proto

echo "Generated $(ls "$out_dir"/*.pb.swift)"
