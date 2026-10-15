/* imxrt1062_t41.ld (framework-arduinoteensy) + cold translation units in flash, see .text.code. */
MEMORY
{
	ITCM (rx):  ORIGIN = 0x00000000, LENGTH = 512K
	DTCM (rw):  ORIGIN = 0x20000000, LENGTH = 512K
	RAM (rw):   ORIGIN = 0x20200000, LENGTH = 512K
	FLASH (rx): ORIGIN = 0x60000000, LENGTH = 7936K
	ERAM (rw):  ORIGIN = 0x70000000, LENGTH = 32768K
}

ENTRY(ImageVectorTable)

SECTIONS
{
	.text.headers : {
		KEEP(*(.flashconfig))
		FILL(0xFF)
		. = ORIGIN(FLASH) + 0x1000;
		KEEP(*(.ivt))
		KEEP(*(.bootdata))
		. = ALIGN(1024);
	} > FLASH

	.text.code : {
		KEEP(*(.startup))
		*(.flashmem*)
		/* MIDI tracker: cold code runs from flash (RAM1 is short). Anything FASTRUN / MT_HOT
		   in these files is in .fastrun and stays in ITCM. */
		*:demos.cpp.o(.text*)
		*demos.cpp.o(.text*)
		*:arp_gen.cpp.o(.text*)
		*arp_gen.cpp.o(.text*)
		*:bass_gen.cpp.o(.text*)
		*bass_gen.cpp.o(.text*)
		*:chord_gen.cpp.o(.text*)
		*chord_gen.cpp.o(.text*)
		*:presets_factory.cpp.o(.text*)
		*presets_factory.cpp.o(.text*)
		*:project_io.cpp.o(.text*)
		*project_io.cpp.o(.text*)
		*:preset_io.cpp.o(.text*)
		*preset_io.cpp.o(.text*)
		*:midi_import.cpp.o(.text*)
		*midi_import.cpp.o(.text*)
		*:inst_codec.cpp.o(.text*)
		*inst_codec.cpp.o(.text*)
		*:link_msg.cpp.o(.text*)
		*link_msg.cpp.o(.text*)
		*:link_fs.cpp.o(.text*)
		*link_fs.cpp.o(.text*)
		*:bank_ops.cpp.o(.text*)
		*bank_ops.cpp.o(.text*)
		*:wt_file.cpp.o(.text*)
		*wt_file.cpp.o(.text*)
		*:sequencer.cpp.o(.text*)
		*sequencer.cpp.o(.text*)
		*:fs_server.cpp.o(.text*)
		*fs_server.cpp.o(.text*)
		*:fw_update.cpp.o(.text*)
		*fw_update.cpp.o(.text*)
		*:sample_tools.cpp.o(.text*)
		*sample_tools.cpp.o(.text*)
		*:render_server.cpp.o(.text*)
		*render_server.cpp.o(.text*)
		*:control_sgtl5000.cpp.o(.text*)
		*control_sgtl5000.cpp.o(.text*)
		*:control_tlv320aic3206.cpp.o(.text*)
		*control_tlv320aic3206.cpp.o(.text*)
		*:ExFatDbg.cpp.o(.text*)
		*ExFatDbg.cpp.o(.text*)
		*:ExFatFormatter.cpp.o(.text*)
		*ExFatFormatter.cpp.o(.text*)
		*:FatFormatter.cpp.o(.text*)
		*FatFormatter.cpp.o(.text*)
		*:FatDbg.cpp.o(.text*)
		*FatDbg.cpp.o(.text*)
		*:CrashReport.cpp.o(.text*)
		*CrashReport.cpp.o(.text*)
		/* Synth modules: per-block code is MT_HOT (.fastrun, ITCM); setup, UI-rate and event code here. */
		*:synth.cpp.o(.text*)
		*synth.cpp.o(.text*)
		*:synth_comp.cpp.o(.text*)
		*synth_comp.cpp.o(.text*)
		*:synth_crush.cpp.o(.text*)
		*synth_crush.cpp.o(.text*)
		*:synth_delay.cpp.o(.text*)
		*synth_delay.cpp.o(.text*)
		*:synth_drive.cpp.o(.text*)
		*synth_drive.cpp.o(.text*)
		*:synth_drum.cpp.o(.text*)
		*synth_drum.cpp.o(.text*)
		*:synth_drum_machines.cpp.o(.text*)
		*synth_drum_machines.cpp.o(.text*)
		*:synth_eq.cpp.o(.text*)
		*synth_eq.cpp.o(.text*)
		*:synth_filter.cpp.o(.text*)
		*synth_filter.cpp.o(.text*)
		*:synth_fm.cpp.o(.text*)
		*synth_fm.cpp.o(.text*)
		*:synth_fm_machines.cpp.o(.text*)
		*synth_fm_machines.cpp.o(.text*)
		*:synth_macro.cpp.o(.text*)
		*synth_macro.cpp.o(.text*)
		*:synth_model.cpp.o(.text*)
		*synth_model.cpp.o(.text*)
		*:synth_modfx.cpp.o(.text*)
		*synth_modfx.cpp.o(.text*)
		*:synth_osc.cpp.o(.text*)
		*synth_osc.cpp.o(.text*)
		*:synth_perf.cpp.o(.text*)
		*synth_perf.cpp.o(.text*)
		*:synth_reverb.cpp.o(.text*)
		*synth_reverb.cpp.o(.text*)
		*:synth_shimmer.cpp.o(.text*)
		*synth_shimmer.cpp.o(.text*)
		*:synth_string.cpp.o(.text*)
		*synth_string.cpp.o(.text*)
		*:synth_syn.cpp.o(.text*)
		*synth_syn.cpp.o(.text*)
		*:synth_vfilter.cpp.o(.text*)
		*synth_vfilter.cpp.o(.text*)
		*:synth_voice.cpp.o(.text*)
		*synth_voice.cpp.o(.text*)
		*:voice_cost.cpp.o(.text*)
		*voice_cost.cpp.o(.text*)
		*:model.cpp.o(.text*)
		*model.cpp.o(.text*)
		*:fx_info.cpp.o(.text*)
		*fx_info.cpp.o(.text*)
		*:render.cpp.o(.text*)
		*render.cpp.o(.text*)
		*:render_link.cpp.o(.text*)
		*render_link.cpp.o(.text*)
		*:perf_fx.cpp.o(.text*)
		*perf_fx.cpp.o(.text*)
		*:scale.cpp.o(.text*)
		*scale.cpp.o(.text*)
		. = ALIGN(4);
		KEEP(*(.init))
		__preinit_array_start = .;
		KEEP (*(.preinit_array))
		__preinit_array_end = .;
		__init_array_start = .;
		KEEP (*(.init_array))
		__init_array_end = .;
		. = ALIGN(4);
	} > FLASH

	.text.progmem : {
		*(.progmem*)
		. = ALIGN(4);
	} > FLASH

	.text.itcm : {
		. = . + 32; /* MPU to trap NULL pointer deref */
		*(.fastrun)
		*(.text*)
		. = ALIGN(16);
	} > ITCM  AT> FLASH

	.ARM.exidx : {
		__exidx_start = .;
		*(.ARM.exidx* .ARM.extab.text* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > ITCM  AT> FLASH

	.data : {
		*(.endpoint_queue)   
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.data*)))
		KEEP(*(.vectorsram))
	} > DTCM  AT> FLASH

	.bss ALIGN(4) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(COMMON)))
		. = ALIGN(32);
		. = . + 32; /* MPU to trap stack overflow */
	} > DTCM

	.bss.dma (NOLOAD) : {
		*(.hab_log)
		*(.dmabuffers)
		. = ALIGN(32);
	} > RAM

	.bss.extram (NOLOAD) : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.externalram)))
		. = ALIGN(32);
	} > ERAM

	.text.csf : {
		FILL(0xFF)
		. = ALIGN(1024);
		KEEP(*(.csf))
		__text_csf_end = .;
	} > FLASH

	_stext = ADDR(.text.itcm);
	_etext = ADDR(.text.itcm) + SIZEOF(.text.itcm) + SIZEOF(.ARM.exidx);
	_stextload = LOADADDR(.text.itcm);

	_sdata = ADDR(.data);
	_edata = ADDR(.data) + SIZEOF(.data);
	_sdataload = LOADADDR(.data);

	_sbss = ADDR(.bss);
	_ebss = ADDR(.bss) + SIZEOF(.bss);

	_heap_start = ADDR(.bss.dma) + SIZEOF(.bss.dma);
	_heap_end = ORIGIN(RAM) + LENGTH(RAM);

	_extram_start = ADDR(.bss.extram);
	_extram_end = ADDR(.bss.extram) + SIZEOF(.bss.extram);

	_itcm_block_count = (SIZEOF(.text.itcm) + SIZEOF(.ARM.exidx) + 0x7FFF) >> 15;
	_flexram_bank_config = 0xAAAAAAAA | ((1 << (_itcm_block_count * 2)) - 1);
	_estack = ORIGIN(DTCM) + ((16 - _itcm_block_count) << 15);

	_flashimagelen = __text_csf_end - ORIGIN(FLASH);
	_teensy_model_identifier = 0x25;

	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }

}
