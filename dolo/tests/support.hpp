// Shared by every test file: GoogleTest, plus the namespaces the tests use unqualified.
#pragma once

#include <gtest/gtest.h>

#include "solodolo.pb.h"

namespace dolo {}
using namespace dolo;
namespace pb = solodolo::v1;
