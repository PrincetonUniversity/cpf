// Last entry must have a comma.

// string.h
"strcmp", // When used before including string.h, clang assumes it's variadic.

// stream
  "_ZNSo3putEc",
  "_ZNSo9_M_insertIdEERSoT_",
  "_ZNSt14basic_ofstreamIcSt11char_traitsIcEEC1EPKcSt13_Ios_Openmode",
  "_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l",

// unistd.h
  "getopt",
  "getopt_long",
  "getopt_long_only",

// Internal
  "\01__isoc99_fscanf", // I don't know where this came from.

// TXIO
  "__txio_compute_epoch",
  "__txio_last_epoch",
  "__txio_free_epoch",
  "__txio_vfprintf",
  "__txio_vprintf",
  "__txio_fprintf",
  "__txio_printf",
  "__txio_fputs",
  "__txio_puts",
  "__txio_fwrite",
  "__txio_fflush",
  "__txio_fclose",
  "__txio_fputc",
  "__txio_putc",
  "__txio_putchar",
  "__txio__IO_putc",
  "__txio_exit",
  "__txio_abort",
  "__txio___assert_fail",
  "__txio_remove",
  "__txio_perror",
