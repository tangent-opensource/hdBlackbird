# Tests

## Leak sanitizer

### Suppression file

```
> ./tests 
[doctest] doctest version is "2.4.6"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:   9 |   9 passed | 0 failed | 0 skipped
[doctest] assertions: 409 | 409 passed | 0 failed |
[doctest] Status: SUCCESS!

=================================================================
==742373==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 1544 byte(s) in 1 object(s) allocated from:
    #0 0x7ff52344eec0 in operator new(unsigned long) (/lib/x86_64-linux-gnu/libasan.so.3+0xc7ec0)
    #1 0x7ff51d42397c in tbb::interface6::internal::ets_base<(tbb::ets_key_usage_type)1>::table_lookup(bool&) [clone .constprop.338] (/opt/hfs18.5/dsolib/libpxr_tf.so+0xab97c)

Direct leak of 35 byte(s) in 1 object(s) allocated from:
    #0 0x7ff52344eec0 in operator new(unsigned long) (/lib/x86_64-linux-gnu/libasan.so.3+0xc7ec0)
    #1 0x7ff51cf05308 in std::string::_Rep::_S_create(unsigned long, unsigned long, std::allocator<char> const&) (/lib/x86_64-linux-gnu/libstdc++.so.6+0xee308)

Direct leak of 16 byte(s) in 2 object(s) allocated from:
    #0 0x7ff52344eec0 in operator new(unsigned long) (/lib/x86_64-linux-gnu/libasan.so.3+0xc7ec0)
    #1 0x7ff51f29f78d in void std::vector<std::string, std::allocator<std::string> >::_M_emplace_back_aux<std::string>(std::string&&) (/opt/hfs18.5/dsolib/libIlmImf_sidefx.so.24+0xbd78d)

SUMMARY: AddressSanitizer: 1595 byte(s) leaked in 4 allocation(s).
```

```
> LSAN_OPTIONS=suppressions=/home/bareya/tangent/dev/hdcycles/tests/lsan.supp ./tests
[doctest] doctest version is "2.4.6"
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:   9 |   9 passed | 0 failed | 0 skipped
[doctest] assertions: 409 | 409 passed | 0 failed |
[doctest] Status: SUCCESS!
-----------------------------------------------------
Suppressions used:
  count      bytes template
      3         51 std::string
      1       1544 tbb:*table_lookup*
-----------------------------------------------------

```