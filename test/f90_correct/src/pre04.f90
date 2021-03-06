!
! Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
!
! Licensed under the Apache License, Version 2.0 (the "License");
! you may not use this file except in compliance with the License.
! You may obtain a copy of the License at
!
!     http://www.apache.org/licenses/LICENSE-2.0
!
! Unless required by applicable law or agreed to in writing, software
! distributed under the License is distributed on an "AS IS" BASIS,
! WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
! See the License for the specific language governing permissions and
! limitations under the License.
!
! This tests stringization
! The idea of this came from the C99 spec
!
#define hash_hash # ## #
#define mkstr(_str) #_str
#define in_between(_z) mkstr(_z)
#define join(_x, _y) in_between(_x hash_hash _y)

program p
    logical :: res(1) = .false., expect(1) = .true.
    print *, join(foo, bar)
    if (join(foo, bar) == 'foo ## bar') then
        res(1) = .true.
    endif

    call check(res, expect, 1)
end program
