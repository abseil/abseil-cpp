//
// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/flags/internal/registry.h"

#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/config.h"
#include "absl/flags/usage_config.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

// --------------------------------------------------------------------
// FlagRegistry implementation
//    A FlagRegistry holds all flag objects indexed
//    by their names so that if you know a flag's name you can access or
//    set it.

namespace absl {
namespace flags_internal {
namespace {

void DestroyFlag(CommandLineFlag* flag) NO_THREAD_SAFETY_ANALYSIS {
  // Values are heap allocated for retired and Abseil Flags.
  if (flag->IsRetired() || flag->IsAbseilFlag()) {
    if (flag->cur) Delete(flag->op, flag->cur);
    if (flag->def) Delete(flag->op, flag->def);
  }

  delete flag->locks;

  // CommandLineFlag handle object is heap allocated for non Abseil Flags.
  if (!flag->IsAbseilFlag()) {
    delete flag;
  }
}

// --------------------------------------------------------------------
// FlagRegistry
//    A FlagRegistry singleton object holds all flag objects indexed
//    by their names so that if you know a flag's name (as a C
//    string), you can access or set it.  If the function is named
//    FooLocked(), you must own the registry lock before calling
//    the function; otherwise, you should *not* hold the lock, and
//    the function will acquire it itself if needed.
// --------------------------------------------------------------------

// A map from flag pointer to CommandLineFlag*. Used when registering
// validators.
class FlagPtrMap {
 public:
  void Register(CommandLineFlag* flag) {
    auto& vec = buckets_[BucketForFlag(flag->cur)];
    if (vec.size() == vec.capacity()) {
      // Bypass default 2x growth factor with 1.25 so we have fuller vectors.
      // This saves 4% memory compared to default growth.
      vec.reserve(vec.size() * 1.25 + 0.5);
    }
    vec.push_back(flag);
  }

  CommandLineFlag* FindByPtr(const void* flag_ptr) {
    const auto& flag_vector = buckets_[BucketForFlag(flag_ptr)];
    for (CommandLineFlag* entry : flag_vector) {
      if (entry->cur == flag_ptr) {
        return entry;
      }
    }
    return nullptr;
  }

 private:
  // Instead of std::map, we use a custom hash table where each bucket stores
  // flags in a vector. This reduces memory usage 40% of the memory that would
  // have been used by std::map.
  //
  // kNumBuckets was picked as a large enough prime. As of writing this code, a
  // typical large binary has ~8k (old-style) flags, and this would gives
  // buckets with roughly 50 elements each.
  //
  // Note that reads to this hash table are rare: exactly as many as we have
  // flags with validators. As of writing, a typical binary only registers 52
  // validated flags.
  static constexpr size_t kNumBuckets = 163;
  std::vector<CommandLineFlag*> buckets_[kNumBuckets];

  static int BucketForFlag(const void* ptr) {
    // Modulo a prime is good enough here. On a real program, bucket size stddev
    // after registering 8k flags is ~5 (mean size at 51).
    return reinterpret_cast<uintptr_t>(ptr) % kNumBuckets;
  }
};
constexpr size_t FlagPtrMap::kNumBuckets;

}  // namespace

class FlagRegistry {
 public:
  FlagRegistry() = default;
  ~FlagRegistry() {
    for (auto& p : flags_) {
      DestroyFlag(p.second);
    }
  }

  // Store a flag in this registry.  Takes ownership of *flag.
  // If ptr is non-null, the flag can later be found by calling
  // FindFlagViaPtrLocked(ptr).
  void RegisterFlag(CommandLineFlag* flag, const void* ptr);

  void Lock() EXCLUSIVE_LOCK_FUNCTION(lock_) { lock_.Lock(); }
  void Unlock() UNLOCK_FUNCTION(lock_) { lock_.Unlock(); }

  // Returns the flag object for the specified name, or nullptr if not found.
  // Will emit a warning if a 'retired' flag is specified.
  CommandLineFlag* FindFlagLocked(absl::string_view name);

  // Returns the retired flag object for the specified name, or nullptr if not
  // found or not retired.  Does not emit a warning.
  CommandLineFlag* FindRetiredFlagLocked(absl::string_view name);

  // Returns the flag object whose current-value is stored at flag_ptr.
  CommandLineFlag* FindFlagViaPtrLocked(const void* flag_ptr);

  static FlagRegistry* GlobalRegistry();  // returns a singleton registry

 private:
  friend class FlagSaverImpl;  // reads all the flags in order to copy them
  friend void ForEachFlagUnlocked(
      std::function<void(CommandLineFlag*)> visitor);

  // The map from name to flag, for FindFlagLocked().
  using FlagMap = std::map<absl::string_view, CommandLineFlag*>;
  using FlagIterator = FlagMap::iterator;
  using FlagConstIterator = FlagMap::const_iterator;
  FlagMap flags_;

  FlagPtrMap flag_ptr_map_;

  absl::Mutex lock_;

  // Disallow
  FlagRegistry(const FlagRegistry&);
  FlagRegistry& operator=(const FlagRegistry&);
};

FlagRegistry* FlagRegistry::GlobalRegistry() {
  static FlagRegistry* global_registry = new FlagRegistry;
  return global_registry;
}

namespace {

class FlagRegistryLock {
 public:
  explicit FlagRegistryLock(FlagRegistry* fr) : fr_(fr) { fr_->Lock(); }
  ~FlagRegistryLock() { fr_->Unlock(); }

 private:
  FlagRegistry* const fr_;
};

}  // namespace

void FlagRegistry::RegisterFlag(CommandLineFlag* flag, const void* ptr) {
  FlagRegistryLock registry_lock(this);
  std::pair<FlagIterator, bool> ins =
      flags_.insert(FlagMap::value_type(flag->Name(), flag));
  if (ins.second == false) {  // means the name was already in the map
    CommandLineFlag* old_flag = ins.first->second;
    if (flag->IsRetired() != old_flag->IsRetired()) {
      // All registrations must agree on the 'retired' flag.
      flags_internal::ReportUsageError(
          absl::StrCat(
              "Retired flag '", flag->Name(),
              "' was defined normally in file '",
              (flag->IsRetired() ? old_flag->Filename() : flag->Filename()),
              "'."),
          true);
    } else if (flag->op != old_flag->op) {
      flags_internal::ReportUsageError(
          absl::StrCat("Flag '", flag->Name(),
                       "' was defined more than once but with "
                       "differing types. Defined in files '",
                       old_flag->Filename(), "' and '", flag->Filename(),
                       "' with types '", old_flag->Typename(), "' and '",
                       flag->Typename(), "', respectively."),
          true);
    } else if (old_flag->IsRetired()) {
      // Retired definitions are idempotent. Just keep the old one.
      DestroyFlag(flag);
      return;
    } else if (old_flag->Filename() != flag->Filename()) {
      flags_internal::ReportUsageError(
          absl::StrCat("Flag '", flag->Name(),
                       "' was defined more than once (in files '",
                       old_flag->Filename(), "' and '", flag->Filename(),
                       "')."),
          true);
    } else {
      flags_internal::ReportUsageError(
          absl::StrCat(
              "Something wrong with flag '", flag->Name(), "' in file '",
              flag->Filename(), "'. One possibility: file '", flag->Filename(),
              "' is being linked both statically and dynamically into this "
              "executable. e.g. some files listed as srcs to a test and also "
              "listed as srcs of some shared lib deps of the same test."),
          true);
    }
    // All cases above are fatal, except for the retired flags.
    std::exit(1);
  }

  if (ptr != nullptr) {
    // This must be the first time we're seeing this flag.
    flag_ptr_map_.Register(flag);
  }
}

CommandLineFlag* FlagRegistry::FindFlagLocked(absl::string_view name) {
  FlagConstIterator i = flags_.find(name);
  if (i == flags_.end()) {
    return nullptr;
  }

  if (i->second->IsRetired()) {
    flags_internal::ReportUsageError(
        absl::StrCat("Accessing retired flag '", name, "'"), false);
  }

  return i->second;
}

CommandLineFlag* FlagRegistry::FindRetiredFlagLocked(absl::string_view name) {
  FlagConstIterator i = flags_.find(name);
  if (i == flags_.end() || !i->second->IsRetired()) {
    return nullptr;
  }

  return i->second;
}

CommandLineFlag* FlagRegistry::FindFlagViaPtrLocked(const void* flag_ptr) {
  return flag_ptr_map_.FindByPtr(flag_ptr);
}

// --------------------------------------------------------------------
// FlagSaver
// FlagSaverImpl
//    This class stores the states of all flags at construct time,
//    and restores all flags to that state at destruct time.
//    Its major implementation challenge is that it never modifies
//    pointers in the 'main' registry, so global FLAG_* vars always
//    point to the right place.
// --------------------------------------------------------------------

class FlagSaverImpl {
 public:
  // Constructs an empty FlagSaverImpl object.
  FlagSaverImpl() {}
  ~FlagSaverImpl() {
    // reclaim memory from each of our CommandLineFlags
    for (const SavedFlag& src : backup_registry_) {
      Delete(src.op, src.current);
      Delete(src.op, src.default_value);
    }
  }

  // Saves the flag states from the flag registry into this object.
  // It's an error to call this more than once.
  // Must be called when the registry mutex is not held.
  void SaveFromRegistry() {
    assert(backup_registry_.empty());  // call only once!
    SavedFlag saved;
    flags_internal::ForEachFlag([&](flags_internal::CommandLineFlag* flag) {
      if (flag->IsRetired()) return;

      saved.name = flag->Name();
      saved.op = flag->op;
      saved.marshalling_op = flag->marshalling_op;
      {
        absl::MutexLock l(InitFlagIfNecessary(flag));
        saved.validator = flag->validator;
        saved.modified = flag->modified;
        saved.on_command_line = flag->IsSpecifiedOnCommandLine();
        saved.current = Clone(saved.op, flag->cur);
        saved.default_value = Clone(saved.op, flag->def);
        saved.counter = flag->counter;
      }
      backup_registry_.push_back(saved);
    });
  }

  // Restores the saved flag states into the flag registry.  We
  // assume no flags were added or deleted from the registry since
  // the SaveFromRegistry; if they were, that's trouble!  Must be
  // called when the registry mutex is not held.
  void RestoreToRegistry() {
    FlagRegistry* const global_registry = FlagRegistry::GlobalRegistry();
    FlagRegistryLock frl(global_registry);
    for (const SavedFlag& src : backup_registry_) {
      CommandLineFlag* flag = global_registry->FindFlagLocked(src.name);
      // If null, flag got deleted from registry.
      if (!flag) continue;

      bool restored = false;
      {
        absl::Mutex* mu = InitFlagIfNecessary(flag);
        absl::MutexLock l(mu);
        flag->validator = src.validator;
        flag->modified = src.modified;
        flag->on_command_line = src.on_command_line;
        if (flag->counter != src.counter ||
            ChangedDirectly(flag, src.default_value, flag->def)) {
          flag->counter++;
          Copy(src.op, src.default_value, flag->def);
        }
        if (flag->counter != src.counter ||
            ChangedDirectly(flag, src.current, flag->cur)) {
          restored = true;
          flag->counter++;
          Copy(src.op, src.current, flag->cur);
          UpdateCopy(flag, mu);

          // Revalidate the flag because the validator might store state based
          // on the flag's value, which just changed due to the restore.
          // Failing validation is ignored because it's assumed that the flag
          // was valid previously and there's little that can be done about it
          // here, anyway.
          Validate(flag, flag->cur);
        }
      }

      // Log statements must be done when no flag lock is held.
      if (restored) {
        ABSL_INTERNAL_LOG(
            INFO, absl::StrCat("Restore saved value of ", flag->Name(), ": ",
                               Unparse(src.marshalling_op, src.current)));
      }
    }
  }

 private:
  struct SavedFlag {
    absl::string_view name;
    FlagOpFn op;
    FlagMarshallingOpFn marshalling_op;
    int64_t counter;
    bool modified;
    bool on_command_line;
    bool (*validator)();
    const void* current;        // nullptr after restore
    const void* default_value;  // nullptr after restore
  };

  std::vector<SavedFlag> backup_registry_;

  FlagSaverImpl(const FlagSaverImpl&);  // no copying!
  void operator=(const FlagSaverImpl&);
};

FlagSaver::FlagSaver() : impl_(new FlagSaverImpl()) {
  impl_->SaveFromRegistry();
}

void FlagSaver::Ignore() {
  delete impl_;
  impl_ = nullptr;
}

FlagSaver::~FlagSaver() {
  if (!impl_) return;

  impl_->RestoreToRegistry();
  delete impl_;
}

// --------------------------------------------------------------------
// GetAllFlags()
//    The main way the FlagRegistry class exposes its data.  This
//    returns, as strings, all the info about all the flags in
//    the main registry, sorted first by filename they are defined
//    in, and then by flagname.
// --------------------------------------------------------------------

struct FilenameFlagnameLess {
  bool operator()(const CommandLineFlagInfo& a,
                  const CommandLineFlagInfo& b) const {
    int cmp = absl::string_view(a.filename).compare(b.filename);
    if (cmp != 0) return cmp < 0;
    return a.name < b.name;
  }
};

void FillCommandLineFlagInfo(CommandLineFlag* flag,
                             CommandLineFlagInfo* result) {
  result->name = std::string(flag->Name());
  result->type = std::string(flag->Typename());
  result->description = flag->Help();
  result->filename = flag->Filename();

  UpdateModifiedBit(flag);

  absl::MutexLock l(InitFlagIfNecessary(flag));
  result->current_value = flag->CurrentValue();
  result->default_value = flag->DefaultValue();
  result->is_default = !flag->modified;
  result->has_validator_fn = (flag->validator != nullptr);
  result->flag_ptr = flag->IsAbseilFlag() ? nullptr : flag->cur;
}

// --------------------------------------------------------------------

CommandLineFlag* FindCommandLineFlag(absl::string_view name) {
  if (name.empty()) return nullptr;
  FlagRegistry* const registry = FlagRegistry::GlobalRegistry();
  FlagRegistryLock frl(registry);

  return registry->FindFlagLocked(name);
}

CommandLineFlag* FindCommandLineV1Flag(const void* flag_ptr) {
  FlagRegistry* const registry = FlagRegistry::GlobalRegistry();
  FlagRegistryLock frl(registry);

  return registry->FindFlagViaPtrLocked(flag_ptr);
}

CommandLineFlag* FindRetiredFlag(absl::string_view name) {
  FlagRegistry* const registry = FlagRegistry::GlobalRegistry();
  FlagRegistryLock frl(registry);

  return registry->FindRetiredFlagLocked(name);
}

// --------------------------------------------------------------------

void ForEachFlagUnlocked(std::function<void(CommandLineFlag*)> visitor) {
  FlagRegistry* const registry = FlagRegistry::GlobalRegistry();
  for (FlagRegistry::FlagConstIterator i = registry->flags_.begin();
       i != registry->flags_.end(); ++i) {
    visitor(i->second);
  }
}

void ForEachFlag(std::function<void(CommandLineFlag*)> visitor) {
  FlagRegistry* const registry = FlagRegistry::GlobalRegistry();
  FlagRegistryLock frl(registry);
  ForEachFlagUnlocked(visitor);
}

// --------------------------------------------------------------------

void GetAllFlags(std::vector<CommandLineFlagInfo>* OUTPUT) {
  flags_internal::ForEachFlag([&](CommandLineFlag* flag) {
    if (flag->IsRetired()) return;

    CommandLineFlagInfo fi;
    FillCommandLineFlagInfo(flag, &fi);
    OUTPUT->push_back(fi);
  });

  // Now sort the flags, first by filename they occur in, then alphabetically
  std::sort(OUTPUT->begin(), OUTPUT->end(), FilenameFlagnameLess());
}

// --------------------------------------------------------------------

bool RegisterCommandLineFlag(CommandLineFlag* flag, const void* ptr) {
  FlagRegistry::GlobalRegistry()->RegisterFlag(flag, ptr);
  return true;
}

// --------------------------------------------------------------------

bool Retire(FlagOpFn ops, FlagMarshallingOpFn marshalling_ops,
            const char* name) {
  auto* flag = new CommandLineFlag(
      name,
      /*help_text=*/absl::flags_internal::HelpText::FromStaticCString(nullptr),
      /*filename_arg=*/"RETIRED", ops, marshalling_ops,
      /*initial_value_gen=*/nullptr,
      /*retired_arg=*/true, nullptr, nullptr);
  FlagRegistry::GlobalRegistry()->RegisterFlag(flag, nullptr);
  return true;
}

// --------------------------------------------------------------------

bool IsRetiredFlag(absl::string_view name, bool* type_is_bool) {
  assert(!name.empty());
  CommandLineFlag* flag = flags_internal::FindRetiredFlag(name);
  if (flag == nullptr) {
    return false;
  }
  assert(type_is_bool);
  *type_is_bool = flag->IsOfType<bool>();
  return true;
}

}  // namespace flags_internal
}  // namespace absl
