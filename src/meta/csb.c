#include "meta.h"
#include "../coding/coding.h"
#include "cri_utf.h"


/* CSB (Cue Sheet Binary?) - CRI container of memory audio, often together with a .cpk wave bank */
VGMSTREAM * init_vgmstream_csb(STREAMFILE *streamFile) {
    VGMSTREAM *vgmstream = NULL;
    STREAMFILE *temp_sf = NULL;
    off_t subfile_offset;
    size_t subfile_size;
    utf_context *utf = NULL;
    utf_context *utf_sdl = NULL;
    int total_subsongs, target_subsong = streamFile->stream_index;
    int8_t fmt = 0;
    const char *stream_name;


    /* checks */
    if (!check_extensions(streamFile, "csb"))
        goto fail;
    if (read_32bitBE(0x00,streamFile) != 0x40555446) /* "@UTF" */
        goto fail;

    /* .csb is an early, simpler version of .acb+awk (see acb.c) used until ~2013?
     * Can stream from .cpk but this only loads memory data. */
    {
        int rows, sdl_rows, sdl_row, i;
        const char *name;
        const char *row_name;
        const char *sdl_name;
        uint32_t sdl_offset, sdl_size, offset, size;
        uint32_t table_offset = 0x00;
        int8_t ttype;
        int found = 0;


        utf = utf_open(streamFile, table_offset, &rows, &name);
        if (!utf) goto fail;

        if (strcmp(name, "TBLCSB") != 0)
            goto fail;

        /* each TBLCSB row has a name and subtable with actual things:
         * - INFO (TBL_INFO): table type/version
         * - CUE (TBLCUE): base cues
         * - SYNTH (TBLSYN): cue configs
         * - SOUND_ELEMENT (TBLSDL): audio info/data (usually AAX)
         * - ISAAC (TBLISC): 3D config
         * - VOICE_LIMIT_GROUP (TBLVLG): system info?
         * Subtable can be empty but must still appear (0 rows).
         */
        sdl_row = 3; /* should use fixed order */

        /* get SOUND_ELEMENT and table */
        if (!utf_query_string(utf, sdl_row, "name", &row_name) || strcmp(row_name, "SOUND_ELEMENT") != 0)
            goto fail;
        if (!utf_query_s8(utf, sdl_row, "ttype", &ttype) || ttype != 4)
            goto fail;
        if (!utf_query_data(utf, sdl_row, "utf", &sdl_offset, &sdl_size))
            goto fail;

        utf_sdl = utf_open(streamFile, sdl_offset, &sdl_rows, &sdl_name);
        if (!utf_sdl) goto fail;

        if (strcmp(sdl_name, "TBLSDL") != 0)
            goto fail;

        total_subsongs = 0;
        if (target_subsong == 0) target_subsong = 1;

        /* get target subsong */
        for (i = 0; i < sdl_rows; i++) {
            int8_t stmflg;

            if (!utf_query_s8(utf_sdl, i, "stmflg", &stmflg))
                goto fail;

            /* only internal data for now (when 1 this refers to a .cpk subfile probably using "name", has size 0) */
            if (stmflg)
                continue;

            total_subsongs++;
            if (total_subsongs == target_subsong && !found) {

                if (!utf_query_string(utf_sdl, i, "name", &stream_name))
                    goto fail;
                if (!utf_query_data(utf_sdl, i, "data", &offset, &size))
                    goto fail;
                if (!utf_query_s8(utf_sdl, i, "fmt", &fmt))
                    goto fail;
                /* also nch/sfreq/nsmpl info */

                found = 1;
            }
        }

        if (!found) goto fail;
        if (target_subsong < 0 || target_subsong > total_subsongs || total_subsongs < 1) goto fail;

        subfile_offset = /*sdl_offset +*/ offset;
        subfile_size = size;

        /* column exists but can be empty */
        if (subfile_size == 0)
            goto fail;
    }

    //;VGM_LOG("CSB: subfile offset=%lx + %x\n", subfile_offset, subfile_size);


    temp_sf = setup_subfile_streamfile(streamFile, subfile_offset, subfile_size, "aax");
    if (!temp_sf) goto fail;

    switch(fmt) {
        case 0: /* AAX */
        case 6: /* HCA */
            vgmstream = init_vgmstream_aax(temp_sf);
            if (!vgmstream) goto fail;
            break;

        case 4: /* ADPCM_WII */
            vgmstream = init_vgmstream_utf_dsp(temp_sf);
            if (!vgmstream) goto fail;
            break;

        default:
            VGM_LOG("CSB: unknown format %i\n", fmt);
            goto fail;
    }

    vgmstream->num_streams = total_subsongs;
    strncpy(vgmstream->stream_name, stream_name, STREAM_NAME_SIZE-1);

    utf_close(utf);
    utf_close(utf_sdl);
    close_streamfile(temp_sf);
    return vgmstream;

fail:
    utf_close(utf);
    utf_close(utf_sdl);
    close_streamfile(temp_sf);
    close_vgmstream(vgmstream);
    return NULL;
}
