add_lldb_unittest(LLDBMinidumpTests
  MinidumpParserTest.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbTarget
    lldbPluginProcessUtility
    lldbPluginProcessMinidump
    lldbUtilityHelpers
  LINK_COMPONENTS
    Support
  )

set(test_inputs
   linux-i386.dmp
   linux-x86_64.dmp
   linux-x86_64_not_crashed.dmp
   fizzbuzz_no_heap.dmp
   fizzbuzz_wow64.dmp
   bad_duplicate_streams.dmp
   bad_overlapping_streams.dmp)

add_unittest_inputs(LLDBMinidumpTests "${test_inputs}")
