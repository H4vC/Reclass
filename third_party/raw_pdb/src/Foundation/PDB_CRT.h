// Copyright 2011-2022, Molecular Matters GmbH <office@molecular-matters.com>
// See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

#pragma once

// Original raw_pdb forward-declares CRT functions to avoid pulling in headers,
// but this conflicts with MinGW's headers when compiled alongside Qt.
// Include the real headers instead.
#include <cstdio>
#include <cstring>
