// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O1 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefixes=OPT,O1
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefixes=OPT,O2
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefixes=OPT,O3
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -Os -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefixes=OPT,SIZE
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -Oz -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefixes=OPT,SIZE
// OPT: Running pass: FlexFatSanitizerPass
// OPT-NOT: Running pass: FlexFatSanitizerPass
// OPT: Running pass: InlinerPass
// OPT-NOT: Running pass: FlexFatSanitizerFunctionPass
// OPT: Running pass: FlexFatSanitizerPass
// OPT-NOT: Running pass: FlexFatSanitizerPass
// OPT: Running pass: ForceFunctionAttrsPass
// OPT-NOT: Running pass: FlexFatSanitizerPass
// OPT: Running pass: InlinerPass
// O1-NOT: Running pass: ArgumentPromotionPass
// O1-NOT: Running pass: AggressiveInstCombinePass
// O2-NOT: Running pass: ArgumentPromotionPass
// O3: Running pass: ArgumentPromotionPass
// SIZE-NOT: Running pass: ArgumentPromotionPass
// O2: Running pass: AggressiveInstCombinePass
// O3: Running pass: AggressiveInstCombinePass
// SIZE: Running pass: AggressiveInstCombinePass
// OPT-NOT: Running pass: FlexFatSanitizerPass

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=thin -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LTO
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=full -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LTO
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: ForceFunctionAttrsPass
// LTO: Running pass: InlinerPass
// LTO-NOT: Running pass: FlexFatSanitizerPass

// Profiling and frontend callbacks must not be inserted by the second pipeline.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -fprofile-instrument=llvm -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=PGO
// PGO: Running pass: PGOInstrumentationGen
// PGO-NOT: Running pass: PGOInstrumentationGen
// PGO: Running pass: FlexFatSanitizerPass
// PGO-NOT: Running pass: PGOInstrumentationGen
// PGO: Running pass: ForceFunctionAttrsPass
// PGO-NOT: Running pass: PGOInstrumentationGen

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=O0
// O0: Running pass: FlexFatSanitizerPass
// O0: Running pass: FlexFatSanitizerPass
// O0-NOT: Running pass: EarlyCSEPass
// O0-NOT: Running pass: InstCombinePass
// O0-NOT: Running pass: SimplifyCFGPass
// O0-NOT: Running pass: ForceFunctionAttrsPass

int load_value(int *p) { return *p; }
