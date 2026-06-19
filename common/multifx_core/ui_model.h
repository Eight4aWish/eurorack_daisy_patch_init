// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// multifx-core / ui_model.h
// Platform-agnostic navigation state machine extracted from the Seed firmware's
// two-level Bank/Patch UI. No pixels, no GPIO, no millis() — the shell feeds it
// debounced button events and reads back state to render however it likes.
//
// Behaviour (matches Seed v1):
//   PATCH level:  short press -> next patch in bank (signals an FX reset)
//                 long press  -> enter BANK menu (preview = current bank)
//   BANK  level:  short press -> cycle previewed bank
//                 long press  -> confirm previewed bank, patch 0 (FX reset)
//
// This generalises the Seed's fixed 2 banks x 4 patches to any layout, which is
// what lets the unified build grow Bank C (filters/shapers from the Patch SM v1)
// and a Bank D of any size — banks need not be uniform width.

#pragma once

namespace mfx {

enum class NavLevel { Patch, Bank };

struct NavModel {
  static constexpr int kMaxBanks = 8;

  int num_banks = 2;
  // Patches per bank (may differ between banks). Defaults to 4 each.
  int patches[kMaxBanks] = {4, 4, 4, 4, 4, 4, 4, 4};

  NavLevel level        = NavLevel::Patch;
  int      bank         = 0;  // active bank
  int      patch        = 0;  // active patch within bank
  int      preview_bank = 0;  // highlighted bank while in the menu

  int PatchCount(int b) const {
    return (b >= 0 && b < kMaxBanks) ? patches[b] : 1;
  }

  // Returns true when the caller should reset/cross-fade the active effect.
  bool OnShortPress() {
    if (level == NavLevel::Bank) {
      preview_bank = (preview_bank + 1) % num_banks;
      return false;
    }
    patch = (patch + 1) % PatchCount(bank);
    return true;
  }

  // Returns true when the caller should reset/cross-fade the active effect.
  bool OnLongPress() {
    if (level == NavLevel::Bank) {
      bank  = preview_bank;
      patch = 0;
      level = NavLevel::Patch;
      return true;
    }
    preview_bank = bank;
    level = NavLevel::Bank;
    return false;
  }
};

} // namespace mfx
