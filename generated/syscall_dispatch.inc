/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
/* Included by the BIOS trap handler. Expects `struct trap_frame *f` with fields
   d0,d1,d2,d3,a0,a1 (uint32_t) and sets `uint32_t ret`. Each bios_<name>() is
   implemented by the BIOS with the C prototype from generated/syscalls.h. */
switch (f->d0 & 0xFFFF) {
case SYS_exit: bios_exit((int)f->d1); ret = 0; break;
case SYS_putchar: ret = (uint32_t)bios_putchar((int)f->d1); break;
case SYS_getchar: ret = (uint32_t)bios_getchar(); break;
case SYS_pollchar: ret = (uint32_t)bios_pollchar(); break;
case SYS_puts: ret = (uint32_t)bios_puts((const char *)f->a0); break;
case SYS_serial_putc: ret = (uint32_t)bios_serial_putc((int)f->d1); break;
case SYS_serial_getc: ret = (uint32_t)bios_serial_getc(); break;
case SYS_ticks: ret = (uint32_t)bios_ticks(); break;
case SYS_wait_vblank: bios_wait_vblank(); ret = 0; break;
case SYS_video_info: ret = (uint32_t)bios_video_info((struct video_info *)f->a0); break;
case SYS_video_set_page: ret = (uint32_t)bios_video_set_page((int)f->d1); break;
case SYS_open: ret = (uint32_t)bios_open((int)f->d1, (const char *)f->a0); break;
case SYS_close: ret = (uint32_t)bios_close((int)f->d1); break;
case SYS_read: ret = (uint32_t)bios_read((int)f->d1, (unsigned)f->d2, (void *)f->a0); break;
case SYS_write: ret = (uint32_t)bios_write((int)f->d1, (unsigned)f->d2, (const void *)f->a0); break;
case SYS_lseek: ret = (uint32_t)bios_lseek((int)f->d1, (int)f->d2, (int)f->d3); break;
case SYS_opendir: ret = (uint32_t)bios_opendir((const char *)f->a0); break;
case SYS_readdir: ret = (uint32_t)bios_readdir((int)f->d1, (struct dirent *)f->a0); break;
case SYS_closedir: ret = (uint32_t)bios_closedir((int)f->d1); break;
case SYS_sd_read_block: ret = (uint32_t)bios_sd_read_block((uint32_t)f->d1, (void *)f->a0); break;
case SYS_sd_write_block: ret = (uint32_t)bios_sd_write_block((uint32_t)f->d1, (const void *)f->a0); break;
case SYS_set_irq_handler: ret = (uint32_t)bios_set_irq_handler((int)f->d1, (void *)f->a0); break;
case SYS_mem_info: ret = (uint32_t)bios_mem_info((struct mem_info *)f->a0); break;
case SYS_con_set_cursor: ret = (uint32_t)bios_con_set_cursor((int)f->d1, (int)f->d2); break;
case SYS_con_clear: ret = (uint32_t)bios_con_clear(); break;
case SYS_con_info: ret = (uint32_t)bios_con_info((struct con_info *)f->a0); break;
case SYS_exec: ret = (uint32_t)bios_exec((const char *)f->a0, (const char *)f->a1); break;
case SYS_key_event: ret = (uint32_t)bios_key_event(); break;
case SYS_sleep_ms: bios_sleep_ms((uint32_t)f->d1); ret = 0; break;
case SYS_set_audio_cb: ret = (uint32_t)bios_set_audio_cb((void *)f->a0); break;
case SYS_audio_start: ret = (uint32_t)bios_audio_start((unsigned)f->d1); break;
case SYS_audio_next: ret = (uint32_t)bios_audio_next((const uint8_t *)f->a0, (int)f->d1); break;
case SYS_audio_last: ret = (uint32_t)bios_audio_last((const uint8_t *)f->a0, (int)f->d1); break;
default: ret = (uint32_t)E_NOSYS; break;
}
