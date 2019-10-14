Pod::Spec.new do |s|
  s.name     = 'abseil'
  s.version  = '1.20190808'
  s.summary  = 'Abseil Common Libraries (C++) from Google'
  s.homepage = 'https://abseil.io'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'Abseil' => 'abseil-io@googlegroups.com' }
  s.source = {
    :git => 'https://github.com/abseil/abseil-cpp.git',
    :tag => '20190808',
  }
  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.9'
  s.tvos.deployment_target = '10.0'
  s.watchos.deployment_target = '4.0'
  s.subspec 'algorithm' do |s1|
    s1.subspec 'algorithm' do |s2|
      s2.public_header_files = 'absl/algorithm/algorithm.h'
    end
    s1.subspec 'container' do |s2|
      s2.public_header_files = 'absl/algorithm/container.h'
      s2.dependency 'abseil/algorithm/algorithm'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
    end
  end
  s.subspec 'base' do |s1|
    s1.subspec 'atomic_hook' do |s2|
      s2.public_header_files = 'absl/base/internal/atomic_hook.h'
    end
    s1.subspec 'base' do |s2|
      s2.public_header_files = 'absl/base/call_once.h'
                               'absl/base/casts.h'
                               'absl/base/internal/cycleclock.h'
                               'absl/base/internal/low_level_scheduling.h'
                               'absl/base/internal/per_thread_tls.h'
                               'absl/base/internal/spinlock.h'
                               'absl/base/internal/sysinfo.h'
                               'absl/base/internal/thread_identity.h'
                               'absl/base/internal/tsan_mutex_interface.h'
                               'absl/base/internal/unscaledcycleclock.h'
      s2.source_files = 'absl/base/internal/cycleclock.cc'
                        'absl/base/internal/spinlock.cc'
                        'absl/base/internal/sysinfo.cc'
                        'absl/base/internal/thread_identity.cc'
                        'absl/base/internal/unscaledcycleclock.cc'
      s2.dependency 'abseil/base/atomic_hook'
      s2.dependency 'abseil/base/base_internal'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/dynamic_annotations'
      s2.dependency 'abseil/base/log_severity'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/base/spinlock_wait'
      s2.dependency 'abseil/meta/type_traits'
    end
    s1.subspec 'base_internal' do |s2|
      s2.public_header_files = 'absl/base/internal/hide_ptr.h'
                               'absl/base/internal/identity.h'
                               'absl/base/internal/inline_variable.h'
                               'absl/base/internal/invoke.h'
                               'absl/base/internal/scheduling_mode.h'
      s2.dependency 'abseil/meta/type_traits'
    end
    s1.subspec 'bits' do |s2|
      s2.public_header_files = 'absl/base/internal/bits.h'
      s2.dependency 'abseil/base/core_headers'
    end
    s1.subspec 'config' do |s2|
      s2.public_header_files = 'absl/base/config.h'
                               'absl/base/policy_checks.h'
    end
    s1.subspec 'core_headers' do |s2|
      s2.public_header_files = 'absl/base/attributes.h'
                               'absl/base/const_init.h'
                               'absl/base/macros.h'
                               'absl/base/optimization.h'
                               'absl/base/port.h'
                               'absl/base/thread_annotations.h'
      s2.source_files = 'absl/base/internal/thread_annotations.h'
      s2.dependency 'abseil/base/config'
    end
    s1.subspec 'dynamic_annotations' do |s2|
      s2.public_header_files = 'absl/base/dynamic_annotations.h'
      s2.source_files = 'absl/base/dynamic_annotations.cc'
    end
    s1.subspec 'endian' do |s2|
      s2.public_header_files = 'absl/base/internal/endian.h'
                               'absl/base/internal/unaligned_access.h'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
    end
    s1.subspec 'log_severity' do |s2|
      s2.public_header_files = 'absl/base/log_severity.h'
      s2.source_files = 'absl/base/log_severity.cc'
      s2.dependency 'abseil/base/core_headers'
    end
    s1.subspec 'malloc_internal' do |s2|
      s2.public_header_files = 'absl/base/internal/direct_mmap.h'
                               'absl/base/internal/low_level_alloc.h'
      s2.source_files = 'absl/base/internal/low_level_alloc.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/dynamic_annotations'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/base/spinlock_wait'
    end
    s1.subspec 'pretty_function' do |s2|
      s2.public_header_files = 'absl/base/internal/pretty_function.h'
    end
    s1.subspec 'raw_logging_internal' do |s2|
      s2.public_header_files = 'absl/base/internal/raw_logging.h'
      s2.source_files = 'absl/base/internal/raw_logging.cc'
      s2.dependency 'abseil/base/atomic_hook'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/log_severity'
    end
    s1.subspec 'spinlock_wait' do |s2|
      s2.public_header_files = 'absl/base/internal/scheduling_mode.h'
                               'absl/base/internal/spinlock_wait.h'
      s2.source_files = 'absl/base/internal/spinlock_akaros.inc'
                        'absl/base/internal/spinlock_linux.inc'
                        'absl/base/internal/spinlock_posix.inc'
                        'absl/base/internal/spinlock_wait.cc'
                        'absl/base/internal/spinlock_win32.inc'
      s2.dependency 'abseil/base/core_headers'
    end
    s1.subspec 'throw_delegate' do |s2|
      s2.public_header_files = 'absl/base/internal/throw_delegate.h'
      s2.source_files = 'absl/base/internal/throw_delegate.cc'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/raw_logging_internal'
    end
  end
  s.subspec 'container' do |s1|
    s1.subspec 'btree' do |s2|
      s2.public_header_files = 'absl/container/btree_map.h'
                               'absl/container/btree_set.h'
      s2.source_files = 'absl/container/internal/btree.h'
                        'absl/container/internal/btree_container.h'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/throw_delegate'
      s2.dependency 'abseil/container/common'
      s2.dependency 'abseil/container/compressed_tuple'
      s2.dependency 'abseil/container/container_memory'
      s2.dependency 'abseil/container/layout'
      s2.dependency 'abseil/memory/memory'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/types/compare'
      s2.dependency 'abseil/utility/utility'
    end
    s1.subspec 'common' do |s2|
      s2.public_header_files = 'absl/container/internal/common.h'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/types/optional'
    end
    s1.subspec 'compressed_tuple' do |s2|
      s2.public_header_files = 'absl/container/internal/compressed_tuple.h'
      s2.dependency 'abseil/utility/utility'
    end
    s1.subspec 'container_memory' do |s2|
      s2.public_header_files = 'absl/container/internal/container_memory.h'
      s2.dependency 'abseil/memory/memory'
      s2.dependency 'abseil/utility/utility'
    end
    s1.subspec 'fixed_array' do |s2|
      s2.public_header_files = 'absl/container/fixed_array.h'
      s2.dependency 'abseil/algorithm/algorithm'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/dynamic_annotations'
      s2.dependency 'abseil/base/throw_delegate'
      s2.dependency 'abseil/container/compressed_tuple'
      s2.dependency 'abseil/memory/memory'
    end
    s1.subspec 'flat_hash_map' do |s2|
      s2.public_header_files = 'absl/container/flat_hash_map.h'
      s2.dependency 'abseil/algorithm/container'
      s2.dependency 'abseil/container/container_memory'
      s2.dependency 'abseil/container/hash_function_defaults'
      s2.dependency 'abseil/container/raw_hash_map'
      s2.dependency 'abseil/memory/memory'
    end
    s1.subspec 'flat_hash_set' do |s2|
      s2.public_header_files = 'absl/container/flat_hash_set.h'
      s2.dependency 'abseil/algorithm/container'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/container/container_memory'
      s2.dependency 'abseil/container/hash_function_defaults'
      s2.dependency 'abseil/container/raw_hash_set'
      s2.dependency 'abseil/memory/memory'
    end
    s1.subspec 'hash_function_defaults' do |s2|
      s2.public_header_files = 'absl/container/internal/hash_function_defaults.h'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/hash/hash'
      s2.dependency 'abseil/strings/strings'
    end
    s1.subspec 'hash_policy_traits' do |s2|
      s2.public_header_files = 'absl/container/internal/hash_policy_traits.h'
      s2.dependency 'abseil/meta/type_traits'
    end
    s1.subspec 'hashtable_debug' do |s2|
      s2.public_header_files = 'absl/container/internal/hashtable_debug.h'
      s2.dependency 'abseil/container/hashtable_debug_hooks'
    end
    s1.subspec 'hashtable_debug_hooks' do |s2|
      s2.public_header_files = 'absl/container/internal/hashtable_debug_hooks.h'
    end
    s1.subspec 'hashtablez_sampler' do |s2|
      s2.public_header_files = 'absl/container/internal/hashtablez_sampler.h'
      s2.source_files = 'absl/container/internal/hashtablez_sampler.cc'
                        'absl/container/internal/hashtablez_sampler_force_weak_definition.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/container/have_sse'
      s2.dependency 'abseil/debugging/stacktrace'
      s2.dependency 'abseil/memory/memory'
      s2.dependency 'abseil/synchronization/synchronization'
      s2.dependency 'abseil/utility/utility'
    end
    s1.subspec 'have_sse' do |s2|
      s2.public_header_files = 'absl/container/internal/have_sse.h'
    end
    s1.subspec 'inlined_vector' do |s2|
      s2.public_header_files = 'absl/container/inlined_vector.h'
      s2.dependency 'abseil/algorithm/algorithm'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/throw_delegate'
      s2.dependency 'abseil/container/inlined_vector_internal'
      s2.dependency 'abseil/memory/memory'
    end
    s1.subspec 'inlined_vector_internal' do |s2|
      s2.public_header_files = 'absl/container/internal/inlined_vector.h'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/container/compressed_tuple'
      s2.dependency 'abseil/memory/memory'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/types/span'
    end
    s1.subspec 'layout' do |s2|
      s2.public_header_files = 'absl/container/internal/layout.h'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/types/span'
      s2.dependency 'abseil/utility/utility'
    end
    s1.subspec 'node_hash_map' do |s2|
      s2.public_header_files = 'absl/container/node_hash_map.h'
      s2.dependency 'abseil/algorithm/container'
      s2.dependency 'abseil/container/container_memory'
      s2.dependency 'abseil/container/hash_function_defaults'
      s2.dependency 'abseil/container/node_hash_policy'
      s2.dependency 'abseil/container/raw_hash_map'
      s2.dependency 'abseil/memory/memory'
    end
    s1.subspec 'node_hash_policy' do |s2|
      s2.public_header_files = 'absl/container/internal/node_hash_policy.h'
    end
    s1.subspec 'node_hash_set' do |s2|
      s2.public_header_files = 'absl/container/node_hash_set.h'
      s2.dependency 'abseil/algorithm/container'
      s2.dependency 'abseil/container/hash_function_defaults'
      s2.dependency 'abseil/container/node_hash_policy'
      s2.dependency 'abseil/container/raw_hash_set'
      s2.dependency 'abseil/memory/memory'
    end
    s1.subspec 'raw_hash_map' do |s2|
      s2.public_header_files = 'absl/container/internal/raw_hash_map.h'
      s2.dependency 'abseil/base/throw_delegate'
      s2.dependency 'abseil/container/container_memory'
      s2.dependency 'abseil/container/raw_hash_set'
    end
    s1.subspec 'raw_hash_set' do |s2|
      s2.public_header_files = 'absl/container/internal/raw_hash_set.h'
      s2.source_files = 'absl/container/internal/raw_hash_set.cc'
      s2.dependency 'abseil/base/bits'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/endian'
      s2.dependency 'abseil/container/common'
      s2.dependency 'abseil/container/compressed_tuple'
      s2.dependency 'abseil/container/container_memory'
      s2.dependency 'abseil/container/hash_policy_traits'
      s2.dependency 'abseil/container/hashtable_debug_hooks'
      s2.dependency 'abseil/container/hashtablez_sampler'
      s2.dependency 'abseil/container/have_sse'
      s2.dependency 'abseil/container/layout'
      s2.dependency 'abseil/memory/memory'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/utility/utility'
    end
  end
  s.subspec 'debugging' do |s1|
    s1.subspec 'debugging_internal' do |s2|
      s2.public_header_files = 'absl/debugging/internal/address_is_readable.h'
                               'absl/debugging/internal/elf_mem_image.h'
                               'absl/debugging/internal/stacktrace_aarch64-inl.inc'
                               'absl/debugging/internal/stacktrace_arm-inl.inc'
                               'absl/debugging/internal/stacktrace_config.h'
                               'absl/debugging/internal/stacktrace_generic-inl.inc'
                               'absl/debugging/internal/stacktrace_powerpc-inl.inc'
                               'absl/debugging/internal/stacktrace_unimplemented-inl.inc'
                               'absl/debugging/internal/stacktrace_win32-inl.inc'
                               'absl/debugging/internal/stacktrace_x86-inl.inc'
                               'absl/debugging/internal/vdso_support.h'
      s2.source_files = 'absl/debugging/internal/address_is_readable.cc'
                        'absl/debugging/internal/elf_mem_image.cc'
                        'absl/debugging/internal/vdso_support.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/dynamic_annotations'
      s2.dependency 'abseil/base/raw_logging_internal'
    end
    s1.subspec 'demangle_internal' do |s2|
      s2.public_header_files = 'absl/debugging/internal/demangle.h'
      s2.source_files = 'absl/debugging/internal/demangle.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/core_headers'
    end
    s1.subspec 'examine_stack' do |s2|
      s2.public_header_files = 'absl/debugging/internal/examine_stack.h'
      s2.source_files = 'absl/debugging/internal/examine_stack.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/debugging/stacktrace'
      s2.dependency 'abseil/debugging/symbolize'
    end
    s1.subspec 'failure_signal_handler' do |s2|
      s2.public_header_files = 'absl/debugging/failure_signal_handler.h'
      s2.source_files = 'absl/debugging/failure_signal_handler.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/debugging/examine_stack'
      s2.dependency 'abseil/debugging/stacktrace'
    end
    s1.subspec 'leak_check' do |s2|
      s2.public_header_files = 'absl/debugging/leak_check.h'
      s2.source_files = 'absl/debugging/leak_check.cc'
      s2.dependency 'abseil/base/core_headers'
    end
    s1.subspec 'leak_check_disable' do |s2|
      s2.source_files = 'absl/debugging/leak_check_disable.cc'
    end
    s1.subspec 'stacktrace' do |s2|
      s2.public_header_files = 'absl/debugging/stacktrace.h'
      s2.source_files = 'absl/debugging/stacktrace.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/debugging/debugging_internal'
    end
    s1.subspec 'symbolize' do |s2|
      s2.public_header_files = 'absl/debugging/internal/symbolize.h'
                               'absl/debugging/symbolize.h'
      s2.source_files = 'absl/debugging/symbolize.cc'
                        'absl/debugging/symbolize_elf.inc'
                        'absl/debugging/symbolize_unimplemented.inc'
                        'absl/debugging/symbolize_win32.inc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/dynamic_annotations'
      s2.dependency 'abseil/base/malloc_internal'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/debugging/debugging_internal'
      s2.dependency 'abseil/debugging/demangle_internal'
    end
  end
  s.subspec 'flags' do |s1|
    s1.subspec 'config' do |s2|
      s2.public_header_files = 'absl/flags/config.h'
                               'absl/flags/usage_config.h'
      s2.source_files = 'absl/flags/usage_config.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/flags/path_util'
      s2.dependency 'abseil/flags/program_name'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/synchronization/synchronization'
    end
    s1.subspec 'flag' do |s2|
      s2.public_header_files = 'absl/flags/declare.h'
                               'absl/flags/flag.h'
      s2.source_files = 'absl/flags/flag.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/flags/config'
      s2.dependency 'abseil/flags/flag_internal'
      s2.dependency 'abseil/flags/handle'
      s2.dependency 'abseil/flags/marshalling'
      s2.dependency 'abseil/strings/strings'
    end
    s1.subspec 'flag_internal' do |s2|
      s2.public_header_files = 'absl/flags/internal/flag.h'
      s2.source_files = 'absl/flags/internal/flag.cc'
      s2.dependency 'abseil/flags/handle'
      s2.dependency 'abseil/flags/registry'
      s2.dependency 'abseil/synchronization/synchronization'
    end
    s1.subspec 'handle' do |s2|
      s2.public_header_files = 'absl/flags/internal/commandlineflag.h'
      s2.source_files = 'absl/flags/internal/commandlineflag.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/flags/config'
      s2.dependency 'abseil/flags/marshalling'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/synchronization/synchronization'
      s2.dependency 'abseil/types/optional'
    end
    s1.subspec 'marshalling' do |s2|
      s2.public_header_files = 'absl/flags/marshalling.h'
      s2.source_files = 'absl/flags/marshalling.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/strings/str_format'
      s2.dependency 'abseil/strings/strings'
    end
    s1.subspec 'parse' do |s2|
      s2.public_header_files = 'absl/flags/internal/parse.h'
                               'absl/flags/parse.h'
      s2.source_files = 'absl/flags/parse.cc'
      s2.dependency 'abseil/flags/config'
      s2.dependency 'abseil/flags/flag'
      s2.dependency 'abseil/flags/program_name'
      s2.dependency 'abseil/flags/registry'
      s2.dependency 'abseil/flags/usage'
      s2.dependency 'abseil/flags/usage_internal'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/synchronization/synchronization'
    end
    s1.subspec 'path_util' do |s2|
      s2.public_header_files = 'absl/flags/internal/path_util.h'
      s2.dependency 'abseil/strings/strings'
    end
    s1.subspec 'program_name' do |s2|
      s2.public_header_files = 'absl/flags/internal/program_name.h'
      s2.source_files = 'absl/flags/internal/program_name.cc'
      s2.dependency 'abseil/flags/path_util'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/synchronization/synchronization'
    end
    s1.subspec 'registry' do |s2|
      s2.public_header_files = 'absl/flags/internal/registry.h'
                               'absl/flags/internal/type_erased.h'
      s2.source_files = 'absl/flags/internal/registry.cc'
                        'absl/flags/internal/type_erased.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/dynamic_annotations'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/flags/config'
      s2.dependency 'abseil/flags/handle'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/synchronization/synchronization'
    end
    s1.subspec 'usage' do |s2|
      s2.public_header_files = 'absl/flags/usage.h'
      s2.source_files = 'absl/flags/usage.cc'
      s2.dependency 'abseil/flags/usage_internal'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/synchronization/synchronization'
    end
    s1.subspec 'usage_internal' do |s2|
      s2.public_header_files = 'absl/flags/internal/usage.h'
      s2.source_files = 'absl/flags/internal/usage.cc'
      s2.dependency 'abseil/flags/config'
      s2.dependency 'abseil/flags/flag'
      s2.dependency 'abseil/flags/handle'
      s2.dependency 'abseil/flags/path_util'
      s2.dependency 'abseil/flags/program_name'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/synchronization/synchronization'
    end
  end
  s.subspec 'hash' do |s1|
    s1.subspec 'city' do |s2|
      s2.public_header_files = 'absl/hash/internal/city.h'
      s2.source_files = 'absl/hash/internal/city.cc'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/endian'
    end
    s1.subspec 'hash' do |s2|
      s2.public_header_files = 'absl/hash/hash.h'
      s2.source_files = 'absl/hash/internal/hash.cc'
                        'absl/hash/internal/hash.h'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/endian'
      s2.dependency 'abseil/container/fixed_array'
      s2.dependency 'abseil/hash/city'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/numeric/int128'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/types/optional'
      s2.dependency 'abseil/types/variant'
      s2.dependency 'abseil/utility/utility'
    end
  end
  s.subspec 'memory' do |s1|
    s1.subspec 'memory' do |s2|
      s2.public_header_files = 'absl/memory/memory.h'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
    end
  end
  s.subspec 'meta' do |s1|
    s1.subspec 'type_traits' do |s2|
      s2.public_header_files = 'absl/meta/type_traits.h'
      s2.dependency 'abseil/base/config'
    end
  end
  s.subspec 'numeric' do |s1|
    s1.subspec 'int128' do |s2|
      s2.public_header_files = 'absl/numeric/int128.h'
      s2.source_files = 'absl/numeric/int128.cc'
                        'absl/numeric/int128_have_intrinsic.inc'
                        'absl/numeric/int128_no_intrinsic.inc'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
    end
  end
  s.subspec 'random' do |s1|
    s1.subspec 'distributions' do |s2|
      s2.public_header_files = 'absl/random/bernoulli_distribution.h'
                               'absl/random/beta_distribution.h'
                               'absl/random/discrete_distribution.h'
                               'absl/random/distribution_format_traits.h'
                               'absl/random/distributions.h'
                               'absl/random/exponential_distribution.h'
                               'absl/random/gaussian_distribution.h'
                               'absl/random/log_uniform_int_distribution.h'
                               'absl/random/poisson_distribution.h'
                               'absl/random/uniform_int_distribution.h'
                               'absl/random/uniform_real_distribution.h'
                               'absl/random/zipf_distribution.h'
      s2.source_files = 'absl/random/discrete_distribution.cc'
                        'absl/random/gaussian_distribution.cc'
      s2.dependency 'abseil/base/base_internal'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/random/internal/distribution_impl'
      s2.dependency 'abseil/random/internal/distributions'
      s2.dependency 'abseil/random/internal/fast_uniform_bits'
      s2.dependency 'abseil/random/internal/fastmath'
      s2.dependency 'abseil/random/internal/iostream_state_saver'
      s2.dependency 'abseil/random/internal/traits'
      s2.dependency 'abseil/random/internal/uniform_helper'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/types/span'
    end
    s1.subspec 'internal' do |s2|
      s2.subspec 'distribution_caller' do |s3|
        s3.public_header_files = 'absl/random/internal/distribution_caller.h'
      end
      s2.subspec 'distribution_impl' do |s3|
        s3.public_header_files = 'absl/random/internal/distribution_impl.h'
        s3.dependency 'abseil/base/bits'
        s3.dependency 'abseil/base/config'
        s3.dependency 'abseil/numeric/int128'
        s3.dependency 'abseil/random/internal/fastmath'
        s3.dependency 'abseil/random/internal/traits'
      end
      s2.subspec 'distributions' do |s3|
        s3.public_header_files = 'absl/random/internal/distributions.h'
        s3.dependency 'abseil/base/base'
        s3.dependency 'abseil/meta/type_traits'
        s3.dependency 'abseil/random/internal/distribution_caller'
        s3.dependency 'abseil/random/internal/traits'
        s3.dependency 'abseil/random/internal/uniform_helper'
        s3.dependency 'abseil/strings/strings'
      end
      s2.subspec 'fast_uniform_bits' do |s3|
        s3.public_header_files = 'absl/random/internal/fast_uniform_bits.h'
      end
      s2.subspec 'fastmath' do |s3|
        s3.public_header_files = 'absl/random/internal/fastmath.h'
        s3.dependency 'abseil/base/bits'
      end
      s2.subspec 'iostream_state_saver' do |s3|
        s3.public_header_files = 'absl/random/internal/iostream_state_saver.h'
        s3.dependency 'abseil/meta/type_traits'
        s3.dependency 'abseil/numeric/int128'
      end
      s2.subspec 'nanobenchmark' do |s3|
        s3.source_files = 'absl/random/internal/nanobenchmark.cc'
        s3.dependency 'abseil/base/raw_logging_internal'
        s3.dependency 'abseil/random/internal/platform'
        s3.dependency 'abseil/random/internal/randen_engine'
      end
      s2.subspec 'nonsecure_base' do |s3|
        s3.public_header_files = 'absl/random/internal/nonsecure_base.h'
        s3.dependency 'abseil/base/core_headers'
        s3.dependency 'abseil/meta/type_traits'
        s3.dependency 'abseil/random/internal/pool_urbg'
        s3.dependency 'abseil/random/internal/salted_seed_seq'
        s3.dependency 'abseil/random/internal/seed_material'
        s3.dependency 'abseil/strings/strings'
        s3.dependency 'abseil/types/optional'
        s3.dependency 'abseil/types/span'
      end
      s2.subspec 'pcg_engine' do |s3|
        s3.public_header_files = 'absl/random/internal/pcg_engine.h'
        s3.dependency 'abseil/base/config'
        s3.dependency 'abseil/meta/type_traits'
        s3.dependency 'abseil/numeric/int128'
        s3.dependency 'abseil/random/internal/fastmath'
        s3.dependency 'abseil/random/internal/iostream_state_saver'
      end
      s2.subspec 'platform' do |s3|
        s3.public_header_files = 'absl/random/internal/randen_traits.h'
      end
      s2.subspec 'pool_urbg' do |s3|
        s3.public_header_files = 'absl/random/internal/pool_urbg.h'
        s3.source_files = 'absl/random/internal/pool_urbg.cc'
        s3.dependency 'abseil/base/base'
        s3.dependency 'abseil/base/config'
        s3.dependency 'abseil/base/core_headers'
        s3.dependency 'abseil/base/endian'
        s3.dependency 'abseil/base/raw_logging_internal'
        s3.dependency 'abseil/random/internal/randen'
        s3.dependency 'abseil/random/internal/seed_material'
        s3.dependency 'abseil/random/internal/traits'
        s3.dependency 'abseil/random/seed_gen_exception'
        s3.dependency 'abseil/types/span'
      end
      s2.subspec 'randen' do |s3|
        s3.public_header_files = 'absl/random/internal/randen.h'
        s3.source_files = 'absl/random/internal/randen.cc'
        s3.dependency 'abseil/base/raw_logging_internal'
        s3.dependency 'abseil/random/internal/platform'
        s3.dependency 'abseil/random/internal/randen_hwaes'
        s3.dependency 'abseil/random/internal/randen_slow'
      end
      s2.subspec 'randen_engine' do |s3|
        s3.public_header_files = 'absl/random/internal/randen_engine.h'
        s3.dependency 'abseil/meta/type_traits'
        s3.dependency 'abseil/random/internal/iostream_state_saver'
        s3.dependency 'abseil/random/internal/randen'
      end
      s2.subspec 'randen_hwaes' do |s3|
        s3.public_header_files = 'absl/random/internal/randen_detect.h'
                                 'absl/random/internal/randen_hwaes.h'
        s3.source_files = 'absl/random/internal/randen_detect.cc'
        s3.dependency 'abseil/random/internal/platform'
        s3.dependency 'abseil/random/internal/randen_hwaes_impl'
      end
      s2.subspec 'randen_hwaes_impl' do |s3|
        s3.source_files = 'absl/random/internal/randen_hwaes.cc'
                          'absl/random/internal/randen_hwaes.h'
        s3.dependency 'abseil/base/core_headers'
        s3.dependency 'abseil/random/internal/platform'
      end
      s2.subspec 'randen_slow' do |s3|
        s3.public_header_files = 'absl/random/internal/randen_slow.h'
        s3.source_files = 'absl/random/internal/randen_slow.cc'
        s3.dependency 'abseil/random/internal/platform'
      end
      s2.subspec 'salted_seed_seq' do |s3|
        s3.public_header_files = 'absl/random/internal/salted_seed_seq.h'
        s3.dependency 'abseil/container/inlined_vector'
        s3.dependency 'abseil/meta/type_traits'
        s3.dependency 'abseil/random/internal/seed_material'
        s3.dependency 'abseil/types/optional'
        s3.dependency 'abseil/types/span'
      end
      s2.subspec 'seed_material' do |s3|
        s3.public_header_files = 'absl/random/internal/seed_material.h'
        s3.source_files = 'absl/random/internal/seed_material.cc'
        s3.dependency 'abseil/base/core_headers'
        s3.dependency 'abseil/base/raw_logging_internal'
        s3.dependency 'abseil/random/internal/fast_uniform_bits'
        s3.dependency 'abseil/strings/strings'
        s3.dependency 'abseil/types/optional'
        s3.dependency 'abseil/types/span'
      end
      s2.subspec 'traits' do |s3|
        s3.public_header_files = 'absl/random/internal/traits.h'
        s3.dependency 'abseil/base/config'
      end
      s2.subspec 'uniform_helper' do |s3|
        s3.public_header_files = 'absl/random/internal/uniform_helper.h'
        s3.dependency 'abseil/meta/type_traits'
      end
    end
    s1.subspec 'random' do |s2|
      s2.public_header_files = 'absl/random/random.h'
      s2.dependency 'abseil/random/internal/nonsecure_base'
      s2.dependency 'abseil/random/internal/pcg_engine'
      s2.dependency 'abseil/random/internal/pool_urbg'
      s2.dependency 'abseil/random/internal/randen_engine'
      s2.dependency 'abseil/random/distributions'
      s2.dependency 'abseil/random/seed_sequences'
    end
    s1.subspec 'seed_gen_exception' do |s2|
      s2.public_header_files = 'absl/random/seed_gen_exception.h'
      s2.source_files = 'absl/random/seed_gen_exception.cc'
      s2.dependency 'abseil/base/config'
    end
    s1.subspec 'seed_sequences' do |s2|
      s2.public_header_files = 'absl/random/seed_sequences.h'
      s2.source_files = 'absl/random/seed_sequences.cc'
      s2.dependency 'abseil/container/inlined_vector'
      s2.dependency 'abseil/random/internal/nonsecure_base'
      s2.dependency 'abseil/random/internal/pool_urbg'
      s2.dependency 'abseil/random/internal/salted_seed_seq'
      s2.dependency 'abseil/random/internal/seed_material'
      s2.dependency 'abseil/random/seed_gen_exception'
      s2.dependency 'abseil/types/span'
    end
  end
  s.subspec 'strings' do |s1|
    s1.subspec 'internal' do |s2|
      s2.public_header_files = 'absl/strings/internal/char_map.h'
                               'absl/strings/internal/ostringstream.h'
                               'absl/strings/internal/resize_uninitialized.h'
                               'absl/strings/internal/utf8.h'
      s2.source_files = 'absl/strings/internal/ostringstream.cc'
                        'absl/strings/internal/utf8.cc'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/endian'
      s2.dependency 'abseil/meta/type_traits'
    end
    s1.subspec 'str_format' do |s2|
      s2.public_header_files = 'absl/strings/str_format.h'
      s2.dependency 'abseil/strings/str_format_internal'
    end
    s1.subspec 'str_format_internal' do |s2|
      s2.public_header_files = 'absl/strings/internal/str_format/arg.h'
                               'absl/strings/internal/str_format/bind.h'
                               'absl/strings/internal/str_format/checker.h'
                               'absl/strings/internal/str_format/extension.h'
                               'absl/strings/internal/str_format/float_conversion.h'
                               'absl/strings/internal/str_format/output.h'
                               'absl/strings/internal/str_format/parser.h'
      s2.source_files = 'absl/strings/internal/str_format/arg.cc'
                        'absl/strings/internal/str_format/bind.cc'
                        'absl/strings/internal/str_format/extension.cc'
                        'absl/strings/internal/str_format/float_conversion.cc'
                        'absl/strings/internal/str_format/output.cc'
                        'absl/strings/internal/str_format/parser.cc'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/numeric/int128'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/types/span'
    end
    s1.subspec 'strings' do |s2|
      s2.public_header_files = 'absl/strings/ascii.h'
                               'absl/strings/charconv.h'
                               'absl/strings/escaping.h'
                               'absl/strings/match.h'
                               'absl/strings/numbers.h'
                               'absl/strings/str_cat.h'
                               'absl/strings/str_join.h'
                               'absl/strings/str_replace.h'
                               'absl/strings/str_split.h'
                               'absl/strings/string_view.h'
                               'absl/strings/strip.h'
                               'absl/strings/substitute.h'
      s2.source_files = 'absl/strings/ascii.cc'
                        'absl/strings/charconv.cc'
                        'absl/strings/escaping.cc'
                        'absl/strings/internal/charconv_bigint.cc'
                        'absl/strings/internal/charconv_bigint.h'
                        'absl/strings/internal/charconv_parse.cc'
                        'absl/strings/internal/charconv_parse.h'
                        'absl/strings/internal/memutil.cc'
                        'absl/strings/internal/memutil.h'
                        'absl/strings/internal/stl_type_traits.h'
                        'absl/strings/internal/str_join_internal.h'
                        'absl/strings/internal/str_split_internal.h'
                        'absl/strings/match.cc'
                        'absl/strings/numbers.cc'
                        'absl/strings/str_cat.cc'
                        'absl/strings/str_replace.cc'
                        'absl/strings/str_split.cc'
                        'absl/strings/string_view.cc'
                        'absl/strings/substitute.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/bits'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/endian'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/base/throw_delegate'
      s2.dependency 'abseil/memory/memory'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/numeric/int128'
      s2.dependency 'abseil/strings/internal'
    end
  end
  s.subspec 'synchronization' do |s1|
    s1.subspec 'graphcycles_internal' do |s2|
      s2.public_header_files = 'absl/synchronization/internal/graphcycles.h'
      s2.source_files = 'absl/synchronization/internal/graphcycles.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/base_internal'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/malloc_internal'
      s2.dependency 'abseil/base/raw_logging_internal'
    end
    s1.subspec 'kernel_timeout_internal' do |s2|
      s2.public_header_files = 'absl/synchronization/internal/kernel_timeout.h'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/time/time'
    end
    s1.subspec 'synchronization' do |s2|
      s2.public_header_files = 'absl/synchronization/barrier.h'
                               'absl/synchronization/blocking_counter.h'
                               'absl/synchronization/internal/create_thread_identity.h'
                               'absl/synchronization/internal/mutex_nonprod.inc'
                               'absl/synchronization/internal/per_thread_sem.h'
                               'absl/synchronization/internal/waiter.h'
                               'absl/synchronization/mutex.h'
                               'absl/synchronization/notification.h'
      s2.source_files = 'absl/synchronization/barrier.cc'
                        'absl/synchronization/blocking_counter.cc'
                        'absl/synchronization/internal/create_thread_identity.cc'
                        'absl/synchronization/internal/per_thread_sem.cc'
                        'absl/synchronization/internal/waiter.cc'
                        'absl/synchronization/mutex.cc'
                        'absl/synchronization/notification.cc'
      s2.dependency 'abseil/base/atomic_hook'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/base_internal'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/dynamic_annotations'
      s2.dependency 'abseil/base/malloc_internal'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/debugging/stacktrace'
      s2.dependency 'abseil/debugging/symbolize'
      s2.dependency 'abseil/synchronization/graphcycles_internal'
      s2.dependency 'abseil/synchronization/kernel_timeout_internal'
      s2.dependency 'abseil/time/time'
    end
  end
  s.subspec 'time' do |s1|
    s1.subspec 'internal' do |s2|
      s2.subspec 'cctz' do |s3|
        s3.subspec 'civil_time' do |s4|
          s4.public_header_files = 'absl/time/internal/cctz/include/cctz/civil_time.h'
          s4.source_files = 'absl/time/internal/cctz/src/civil_time_detail.cc'
        end
        s3.subspec 'time_zone' do |s4|
          s4.public_header_files = 'absl/time/internal/cctz/include/cctz/time_zone.h'
                                   'absl/time/internal/cctz/include/cctz/zone_info_source.h'
          s4.source_files = 'absl/time/internal/cctz/src/time_zone_fixed.cc'
                            'absl/time/internal/cctz/src/time_zone_fixed.h'
                            'absl/time/internal/cctz/src/time_zone_format.cc'
                            'absl/time/internal/cctz/src/time_zone_if.cc'
                            'absl/time/internal/cctz/src/time_zone_if.h'
                            'absl/time/internal/cctz/src/time_zone_impl.cc'
                            'absl/time/internal/cctz/src/time_zone_impl.h'
                            'absl/time/internal/cctz/src/time_zone_info.cc'
                            'absl/time/internal/cctz/src/time_zone_info.h'
                            'absl/time/internal/cctz/src/time_zone_libc.cc'
                            'absl/time/internal/cctz/src/time_zone_libc.h'
                            'absl/time/internal/cctz/src/time_zone_lookup.cc'
                            'absl/time/internal/cctz/src/time_zone_posix.cc'
                            'absl/time/internal/cctz/src/time_zone_posix.h'
                            'absl/time/internal/cctz/src/tzfile.h'
                            'absl/time/internal/cctz/src/zone_info_source.cc'
          s4.dependency 'abseil/time/internal/cctz/civil_time'
        end
      end
    end
    s1.subspec 'time' do |s2|
      s2.public_header_files = 'absl/time/civil_time.h'
                               'absl/time/clock.h'
                               'absl/time/time.h'
      s2.source_files = 'absl/time/civil_time.cc'
                        'absl/time/clock.cc'
                        'absl/time/duration.cc'
                        'absl/time/format.cc'
                        'absl/time/internal/get_current_time_chrono.inc'
                        'absl/time/internal/get_current_time_posix.inc'
                        'absl/time/time.cc'
      s2.dependency 'abseil/base/base'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/raw_logging_internal'
      s2.dependency 'abseil/numeric/int128'
      s2.dependency 'abseil/strings/strings'
      s2.dependency 'abseil/time/internal/cctz/civil_time'
      s2.dependency 'abseil/time/internal/cctz/time_zone'
    end
  end
  s.subspec 'types' do |s1|
    s1.subspec 'any' do |s2|
      s2.public_header_files = 'absl/types/any.h'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/types/bad_any_cast'
      s2.dependency 'abseil/utility/utility'
    end
    s1.subspec 'bad_any_cast' do |s2|
      s2.public_header_files = 'absl/types/bad_any_cast.h'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/types/bad_any_cast_impl'
    end
    s1.subspec 'bad_any_cast_impl' do |s2|
      s2.source_files = 'absl/types/bad_any_cast.cc'
                        'absl/types/bad_any_cast.h'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/raw_logging_internal'
    end
    s1.subspec 'bad_optional_access' do |s2|
      s2.public_header_files = 'absl/types/bad_optional_access.h'
      s2.source_files = 'absl/types/bad_optional_access.cc'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/raw_logging_internal'
    end
    s1.subspec 'bad_variant_access' do |s2|
      s2.public_header_files = 'absl/types/bad_variant_access.h'
      s2.source_files = 'absl/types/bad_variant_access.cc'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/raw_logging_internal'
    end
    s1.subspec 'compare' do |s2|
      s2.public_header_files = 'absl/types/compare.h'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
    end
    s1.subspec 'optional' do |s2|
      s2.public_header_files = 'absl/types/optional.h'
      s2.source_files = 'absl/types/internal/optional.h'
      s2.dependency 'abseil/base/base_internal'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/memory/memory'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/types/bad_optional_access'
      s2.dependency 'abseil/utility/utility'
    end
    s1.subspec 'span' do |s2|
      s2.public_header_files = 'absl/types/span.h'
      s2.source_files = 'absl/types/internal/span.h'
      s2.dependency 'abseil/algorithm/algorithm'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/base/throw_delegate'
      s2.dependency 'abseil/meta/type_traits'
    end
    s1.subspec 'variant' do |s2|
      s2.public_header_files = 'absl/types/variant.h'
      s2.source_files = 'absl/types/internal/variant.h'
      s2.dependency 'abseil/base/base_internal'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/base/core_headers'
      s2.dependency 'abseil/meta/type_traits'
      s2.dependency 'abseil/types/bad_variant_access'
      s2.dependency 'abseil/utility/utility'
    end
  end
  s.subspec 'utility' do |s1|
    s1.subspec 'utility' do |s2|
      s2.public_header_files = 'absl/utility/utility.h'
      s2.dependency 'abseil/base/base_internal'
      s2.dependency 'abseil/base/config'
      s2.dependency 'abseil/meta/type_traits'
    end
  end
end
