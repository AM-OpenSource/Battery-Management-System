This is an adaptation of ChaN's FatFS FAT filesystem library based on v0.12.

Common files are ff.c, ff.h, ffconf.h, integer.h, diskio.h. These should be
updated when a new version of ChaN FAT library is released. Ensure that any
changes to ffconf.h are reflected in the new version.

The remaining files adapt the library to the hardware being used, to FreeRTOS
and to libopencm3. Some changes to these may be necessary if the ChaN FAT API
changes.

Currently the makefile calls on sd_spi_loc3_stm32_freertos.c. The other files
are older attempts that have been retained for possible future reference
but are outdated.

More information is available on [Jiggerjuice](http://www.jiggerjuice.info/electronics/projects/solarbms/solarbms-software.html).

(c) K. Sarkies 10/12/2016

