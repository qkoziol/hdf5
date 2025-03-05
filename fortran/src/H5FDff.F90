!> @defgroup FH5FD Fortran VFD (H5FD) Interface
!!
!! @see H5FD, C-API
!!
!! @see @ref H5FD_UG, User Guide
!!

!> @ingroup FH5FD
!!
!! @brief This module contains Fortran interfaces for H5FD (VFD) functions.
!
! COPYRIGHT
! * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
!   Copyright by The HDF Group.                                               *
!   All rights reserved.                                                      *
!                                                                             *
!   This file is part of HDF5.  The full HDF5 copyright notice, including     *
!   terms governing use, modification, and redistribution, is contained in    *
!   the LICENSE file, which can be found at the root of the source code       *
!   distribution tree, or in https://www.hdfgroup.org/licenses.               *
!   If you do not have access to either file, you may request a copy from     *
!   help@hdfgroup.org.                                                        *
! * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
!
! NOTES
!       _____ __  __ _____   ____  _____ _______       _   _ _______
!      |_   _|  \/  |  __ \ / __ \|  __ \__   __|/\   | \ | |__   __|
! ****   | | | \  / | |__) | |  | | |__) | | |  /  \  |  \| |  | |    ****
! ****   | | | |\/| |  ___/| |  | |  _  /  | | / /\ \ | . ` |  | |    ****
! ****  _| |_| |  | | |    | |__| | | \ \  | |/ ____ \| |\  |  | |    ****
!      |_____|_|  |_|_|     \____/|_|  \_\ |_/_/    \_\_| \_|  |_|
!
!  If you add a new H5FD function you must add the function name to the
!  Windows dll file 'hdf5_fortrandll.def.in' in the fortran/src directory.
!  This is needed for Windows based operating systems.
!

MODULE H5FD

  USE H5GLOBAL
  USE H5fortkit

  IMPLICIT NONE

CONTAINS


!>
!! \ingroup FH5FD
!!
!! \brief Determines whether two driver identifiers refer to the same driver.
!!
!! \param drvr_id1 A valid identifier of the first driver to check
!! \param drvr_id2 A valid identifier of the second driver to check
!! \param are_same Whether driver IDs refer to the same driver
!! \param hdferr    \fortran_error
!!
!! See C API: @ref H5FDcmp_driver_cls()
!!
  SUBROUTINE H5FDcmp_driver_cls_f(are_same, drvr_id1, drvr_id2, hdferr)
    IMPLICIT NONE
    LOGICAL, INTENT(OUT) :: are_same
    INTEGER(HID_T), INTENT(IN) :: drvr_id1
    INTEGER(HID_T), INTENT(IN) :: drvr_id2
    INTEGER, INTENT(OUT) :: hdferr

    INTEGER(C_INT) :: are_same_c

    INTERFACE
       INTEGER(C_INT) FUNCTION H5FDcmp_driver_cls(cmp_value, drvr_id1, drvr_id2) BIND(C, NAME='H5FDcmp_driver_cls')
         IMPORT :: HID_T, C_INT
         INTEGER(C_INT), INTENT(OUT) :: cmp_value
         INTEGER(HID_T), VALUE :: drvr_id1
         INTEGER(HID_T), VALUE :: drvr_id2
       END FUNCTION H5FDcmp_driver_cls
    END INTERFACE

    are_same = .FALSE.
    hdferr = INT(H5FDcmp_driver_cls(are_same_c, drvr_id1, drvr_id2))
    IF(are_same_c .EQ. 0_C_INT) are_same = .TRUE.

  END SUBROUTINE H5FDcmp_driver_cls_f

END MODULE H5FD
