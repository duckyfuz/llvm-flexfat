; FlexFat: the unrecognized-producer fallback is a VISIBLE signal, not a silent
; unsound elision. An atomicrmw result is a pointer producer getPtrBounds does
; not recognize: the (BUG) warning fires (and NumUnknownProducers is bumped) and
; the check is still elided (NONFAT, matching the reference default). The corpus
; canaries (bounds.ll / unknown_producer.ll, --implicit-check-not) assert this
; never fires on real code; if it does, the producer must be added to
; getPtrBounds rather than silently elided. This is the negative control proving
; the signal actually fires.
;
; RUN: opt < %s -passes=flexfat -S -o /dev/null 2> %t.err
; RUN: FileCheck %s --check-prefix=WARN < %t.err
; RUN: opt < %s -passes=flexfat -S 2>/dev/null | FileCheck %s --check-prefix=IR

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i8 @atomic_producer(ptr %pp, ptr %v) {
  %p = atomicrmw xchg ptr %pp, ptr %v seq_cst, align 8
  %x = load i8, ptr %p
  ret i8 %x
}

; The fallback warning fires, naming the unrecognized opcode:
; WARN: FlexFat: (BUG) unknown pointer type in static bounds analysis ('atomicrmw')

; ...and the check is elided (NONFAT default), so no instrumentation is emitted:
; IR-LABEL: define i8 @atomic_producer(
; IR-NOT:   call void @lowfat_oob_error
