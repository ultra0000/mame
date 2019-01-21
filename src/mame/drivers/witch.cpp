// license:BSD-3-Clause
// copyright-holders:Tomasz Slanina
/*

Witch / Pinball Champ '95


            Witch: press F1 (Memory Reset) to initialize NVRAM

Pinball Champ '95: Seems to be a simple mod with the following differences:
                   -The title screen is changed
                   -The sample saying "witch" is not played (obviously)
                   -Different configuration values (time limit, etc)
                   -Auto-initialization on NVRAM error(?)
                   -Stars keep falling at the title screen


ES-9104 PCB:
+-------------------------------------+
|        12.00MHz                5.1A |
|                                     |
|   6116                         6116 |
|   6116                         6116 |
|          24S10N                     |
|                                     |
|       SW2         Z80A     6116     |
|SW1 8255     SW5   Z80A              |
|                                     |
|      SW3                       6116 |
|      SW4 YM2203                     |
|          YM2203    U_5B.5U   3.3U   |
|X2 M5202  1.10V     HM6264           |
|  VR1     ES8712    BAT1             |
+-------------------------------------+

       Z80A: Two Z80A CPUs frequency unknown (4MHz? 12MHz/3) (CPU2 used mainly for sound effects)
     YM2203: Two Yamaha YM2203+YM3014B sound chip combos. Frequency unknown (music + sound effects + video scrolling access)
      M5202: OKI M5202 ADPCM Speech Synthesis IC @ 384kHz
     ES8712: Excellent System ES-8712 Streaming single channel ADPCM, frequency unknown (samples)
       8255: M5M82C255ASP Programmable Peripheral Interface
        OSC: 12.000MHz
         X2: 384kHz Resonator to drive M5202
        VR1: Volume pot
       BAT1: 3.6V battery
        SW1: Service push button
      other: 8-position dipswitch x 4 (labeled SW2 through SW5)
             Standard 8-liner harness connectors (36x2 edge connector + 10x2 edge connector).


This is so far what could be reverse-engineered from the code.
BEWARE : these are only suppositions, not facts.


GFX

    2 gfx layers accessed by cpu1 (& cpu2 for scrolling) + 1 sprite layer

    In (assumed) order of priority :
        - Top layer @0xc000-0xc3ff(vram) + 0xc400-0xc7ff(cram) apparently not scrollable (gfx0)
            Uses tiles from "gfx2"

            tileno =    vram | ((cram & 0xe0) << 3)
            color  =    cram & 0x0f
            priority =  cram & 0x10 (0x10 = under sprites, 0x00 = over sprites)

        - Sprites @0xd000-0xd7ff + 0xd800-0xdfff
                One sprite every 0x20 bytes
                0x40 sprites
                Tiles are from "gfx2"
                Seems to be only 16x16 sprites (2x2 tiles)
                xflip and yflip available

                tileno                = sprite_ram[i*0x20] << 2 | (( sprite_ram[i*0x20+0x800] & 0x07 ) << 10 );
                sx                    = sprite_ram[i*0x20+1];
                sy              = sprite_ram[i*0x20+2];
                flags+colors    = sprite_ram[i*0x20+3];

        - Background layer @0xc800-0xcbff(vram) + 0xcc00-0xcfff(cram) (gfx1)
                Uses tiles from "gfx1"
                    tileno = vram | ((cram & 0xf0) << 4),
                    color  = cram & 0x0f

                The background is scrolled via 2 registers accessed through one of the ym2203, port A&B
                The scrolling is set by CPU2 in its interrupt handler.
                CPU1 doesn't seem to offset vram accesses for the scrolling, so it's assumed to be done
                in hardware.
                This layer looks misaligned with the sprites, but the top layer is not. This is perhaps
                due to the weird handling of the scrolling. For now we just offset it by 7 pixels.


Palette

    3*0x100 palette banks @ 0xe000-0xe300 & 0xe800-0xe8ff (xBBBBBGGGGGRRRRR_split format?)
    Bank 1 is used for gfx0 (top layer) and sprites
    Bank 2 is for gfx1 (background layer)

    Could not find any use of bank 0 ; I'm probably missing a flag somewhere.


Sound

    Mainly handled by CPU2

    2xYM2203

    0x8000-0x8001 : Mainly used for sound effects & to read dipswitches
    0x8008-0x8009 : Music & scrolling

    1xES8712

    Mapped @0x8010-0x8016
    Had to patch es8712.cpp to start playing on 0x8016 write and to prevent continuous looping.
    There's a test on bit1 at offset 0 (0x8010), so this may be a "read status" kind of port.


Ports

    0xA000-0xA00f : Various ports yet to figure out...

      - 0xA000 : unknown ; seems muxed with a002
      - 0xA002 : banking?
                         bank number = bits 7&6 (swapped?)
                 mapped 0x0800-0x7fff?
                 0x0000-0x07ff ignored?
                 see code @ 61d
                 lower bits seems to mux port A000 reads
      - 0xA003 : ?
      - 0xA004 : dipswitches
      - 0xA005 : dipswitches
      - 0xA006 : bit1(out) = release coin?
      - 0xA007 : ?
      - 0xA008 : cpu1 sets it to 0x80 on reset ; cleared in interrupt handler
                             cpu2 sets it to 0x40 on reset ; cleared in interrupt handler
      - 0xA00C : bit0 = payout related?
                         bit3 = reset? (see cpu2 code @14C)
      - 0xA00E : ?


Memory

    RAM:
        Considering that
            -CPU1 busy loops on fd00 and that CPU2 modifies fd00 once it is initialized
            -CPU1 writes to fd01-fd05 and CPU2 reads there and plays sounds accordingly
            -CPU1 writes to f208-f209 and CPU2 forwards this to the scrolling registers
        we can assume that the 0xf2xx and 0fdxx segments are shared.

        From the fact that
            -CPU1's SP is set to 0xf100 on reset
            -CPU2's SP is set to 0xf080 on reset
        we may suppose that this memory range (0xf000-0xf0ff) is shared too.

        Moreover, range 0xf100-0xf1ff is checked after reset without prior initialization and
        is being reset ONLY by changing a particular port bit whose modification ends up with
        a soft reboot. This looks like a good candidate for an NVRAM segment.
        Whether CPU2 can access the NVRAM or not is still a mystery considering that it never
        attempts to do so.

        From these we consider that the 0xfxxx segment, except for the NVRAM range, is shared
        between the two CPUs.

  CPU1:
      The ROM segment (0x0000-0x7fff) is banked, but the 0x0000-0x07ff region does not look
      like being affected (the SEGA Master System did something similar IIRC). A particular
      bank is selected by changing the two most significant bits of port 0xa002 (swapped?).

  CPU2:
            No banking
        Doesn't seem to be banking going on. However there's a strange piece of code @0x021a:
        Protection(?) check @ $21a


Interesting memory locations

        +f180-f183 : dipswitches stored here (see code@2746). Beware, all values are "CPL"ed!
            *f180   : kkkbbppp / A005
                             ppp  = PAY OUT | 60 ; 65 ; 70 ; 75 ; 80 ; 85 ; 90 ; 95
                             bb   = MAX BET | 20 ; 30 ; 40 ; 60
                             kkk  = KEY IN  | 1-10 ; 1-20 ; 1-40 ; 1-50 ; 1-100 ; 1-200 ; 1-250 ; 1-500

            *f181   : ccccxxxd / A004
                             d    = DOUBLE UP | ON ; OFF
                             cccc = COIN IN1 | 1-1 ; 1-2 ; 1-3 ; 1-4 ; 1-5 ; 1-6 ; 1-7 ; 1-8 ; 1-9 ; 1-10 ; 1-15 ; 1-20 ; 1-25 ; 1-30 ; 1-40 ; 1-50

            *f182   : sttpcccc / portA
                             cccc = COIN IN2 | 1-1 ; 1-2 ; 1-3 ; 1-4 ; 1-5 ; 1-6 ; 1-7 ; 1-8 ; 1-9 ; 1-10 ; 2-1 ; 3-1 ; 4-1 ; 5-1 ; 6-1 ; 10-1
                             p    = PAYOUT SWITCH | ON ; OFF
                             tt   = TIME | 40 ; 45 ; 50 ; 55
                             s    = DEMO SOUND | ON ; OFF
            *f183 : xxxxhllb / portB
                             b    = AUTO BET | ON ; OFF
                             ll   = GAME LIMIT | 500 ; 1000 ; 5000 ; 990000
                             h    = HOPPER ACTIVE | LOW ; HIGH


        +f15c-f15e : MAX WIN
        +f161      : JACK POT
        +f166-f168 : DOUBLE UP
        +f16b-f16d : MAX D-UP WIN

        +f107-f109 : TOTAL IN
        +f10c-f10e : TOTAL OUT

        +f192-f194 : credits (bcd)

        +fd00 = cpu2 ready
        +f211 = input port cache?

    CPU2 Commands :
        -0xfd01 start music
        -0xfd02 play sound effect
        -0xfd03 play sample on the ES8712
        -0xfd04 ?
        -0xfd05 ?


TODO :
    - Figure out the ports for the "PayOut" stuff (a006/a00c?);
    - Hook up the OKI M5202;
    - lagging sprites on witch (especially noticeable when game scrolls up/down)
*/

#include "emu.h"
#include "includes/witch.h"


TILE_GET_INFO_MEMBER(witch_state::get_gfx0b_tile_info)
{
	int code  = m_gfx0_vram[tile_index];
	int color = m_gfx0_cram[tile_index];

	code=code | ((color & 0xe0) << 3);

	if(color&0x10)
	{
		code=0;
	}

	SET_TILE_INFO_MEMBER(1,
			code,   //tiles beyond 0x7ff only for sprites?
			color & 0x0f,
			0);
}

TILE_GET_INFO_MEMBER(witch_state::get_gfx0a_tile_info)
{
	int code  = m_gfx0_vram[tile_index];
	int color = m_gfx0_cram[tile_index];

	code=code | ((color & 0xe0) << 3);

	if((color&0x10)==0)
	{
		code=0;
	}

	SET_TILE_INFO_MEMBER(1,
			code,//tiles beyond 0x7ff only for sprites?
			color & 0x0f,
			0);
}

TILE_GET_INFO_MEMBER(witch_state::get_gfx1_tile_info)
{
	int code  = m_gfx1_vram[tile_index];
	int color = m_gfx1_cram[tile_index];

	SET_TILE_INFO_MEMBER(0,
			code | ((color & 0xf0) << 4),
			(color>>0) & 0x0f,
			0);
}

TILE_GET_INFO_MEMBER(keirinou_state::get_keirinou_gfx1_tile_info)
{
	int code  = m_gfx1_vram[tile_index];
	int color = m_gfx1_cram[tile_index];

	SET_TILE_INFO_MEMBER(0,
			code | ((color & 0xc0) << 2) | (m_bg_bank << 10),
			(color>>0) & 0xf,
			0);
}

void witch_state::video_common_init()
{
	m_gfx0a_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(FUNC(witch_state::get_gfx0a_tile_info),this),TILEMAP_SCAN_ROWS,8,8,32,32);
	m_gfx0b_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(FUNC(witch_state::get_gfx0b_tile_info),this),TILEMAP_SCAN_ROWS,8,8,32,32);

	m_gfx0a_tilemap->set_transparent_pen(0);
	m_gfx0b_tilemap->set_transparent_pen(0);

	save_item(NAME(m_scrollx));
	save_item(NAME(m_scrolly));
	save_item(NAME(m_reg_a002));
	save_item(NAME(m_motor_active));
}

void witch_state::video_start()
{
	video_common_init();
	m_gfx1_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(FUNC(witch_state::get_gfx1_tile_info),this),TILEMAP_SCAN_ROWS,8,8,32,32);

	m_gfx0a_tilemap->set_palette_offset(0x100);
	m_gfx0b_tilemap->set_palette_offset(0x100);
	m_gfx1_tilemap->set_palette_offset(0x200);

	has_spr_rom_bank = false;
}

void keirinou_state::video_start()
{
	//witch_state::video_start();
	video_common_init();
	m_gfx1_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(FUNC(keirinou_state::get_keirinou_gfx1_tile_info),this),TILEMAP_SCAN_ROWS,8,8,32,32);

	m_gfx0a_tilemap->set_palette_offset(0x000);
	m_gfx0b_tilemap->set_palette_offset(0x000);
	m_gfx1_tilemap->set_palette_offset(0x100);

	save_item(NAME(m_spr_bank));
	save_item(NAME(m_bg_bank));

	has_spr_rom_bank = true;
}

void witch_state::draw_sprites(bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	int i,sx,sy,tileno,flags,color;
	int flipx=0;
	int flipy=0;
	int region = has_spr_rom_bank == true ? 2 : 1;

	for(i=0;i<0x800;i+=0x20) {
		sx     = m_sprite_ram[i+1];
		if(sx!=0xF8) {
			tileno = (m_sprite_ram[i]<<2);
			tileno+= (has_spr_rom_bank == true ? m_spr_bank : ( m_sprite_ram[i+0x800] & 0x07 )) << 10;

			sy     = m_sprite_ram[i+2];
			flags  = m_sprite_ram[i+3];

			flipx  = (flags & 0x10 ) ? 1 : 0;
			flipy  = (flags & 0x20 ) ? 1 : 0;

			color  =  (flags & 0x0f);


			m_gfxdecode->gfx(region)->transpen(bitmap,cliprect,
				tileno, color,
				flipx, flipy,
				sx+8*flipx,sy+8*flipy,0);

			m_gfxdecode->gfx(region)->transpen(bitmap,cliprect,
				tileno+1, color,
				flipx, flipy,
				sx+8-8*flipx,sy+8*flipy,0);

			m_gfxdecode->gfx(region)->transpen(bitmap,cliprect,
				tileno+2, color,
				flipx, flipy,
				sx+8*flipx,sy+8-8*flipy,0);

			m_gfxdecode->gfx(region)->transpen(bitmap,cliprect,
				tileno+3, color,
				flipx, flipy,
				sx+8-8*flipx,sy+8-8*flipy,0);

		}
	}
}

uint32_t witch_state::screen_update_witch(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	m_gfx1_tilemap->set_scrollx(0, m_scrollx-7 ); //offset to have it aligned with the sprites
	m_gfx1_tilemap->set_scrolly(0, m_scrolly+8 );

	m_gfx1_tilemap->draw(screen, bitmap, cliprect, 0,0);
	m_gfx0a_tilemap->draw(screen, bitmap, cliprect, 0,0);
	draw_sprites(bitmap, cliprect);
	m_gfx0b_tilemap->draw(screen, bitmap, cliprect, 0,0);
	return 0;
}

WRITE8_MEMBER(witch_state::gfx0_vram_w)
{
	m_gfx0_vram[offset] = data;
	m_gfx0a_tilemap->mark_tile_dirty(offset);
	m_gfx0b_tilemap->mark_tile_dirty(offset);
}

WRITE8_MEMBER(witch_state::gfx0_cram_w)
{
	m_gfx0_cram[offset] = data;
	m_gfx0a_tilemap->mark_tile_dirty(offset);
	m_gfx0b_tilemap->mark_tile_dirty(offset);
}

#define FIX_OFFSET() do { \
	offset=(((offset + ((m_scrolly & 0xf8) << 2) ) & 0x3e0)+((offset + (m_scrollx >> 3) ) & 0x1f)+32)&0x3ff; } while(0)

WRITE8_MEMBER(witch_state::gfx1_vram_w)
{
	FIX_OFFSET();
	m_gfx1_vram[offset] = data;
	m_gfx1_tilemap->mark_tile_dirty(offset);
}

WRITE8_MEMBER(witch_state::gfx1_cram_w)
{
	FIX_OFFSET();
	m_gfx1_cram[offset] = data;
	m_gfx1_tilemap->mark_tile_dirty(offset);
}
READ8_MEMBER(witch_state::gfx1_vram_r)
{
	FIX_OFFSET();
	return m_gfx1_vram[offset];
}

READ8_MEMBER(witch_state::gfx1_cram_r)
{
	FIX_OFFSET();
	return m_gfx1_cram[offset];
}

READ8_MEMBER(witch_state::read_a000)
{
	switch (m_reg_a002 & 0x3f)
	{
		case 0x3b:
			return ioport("UNK")->read();   //bet10 / pay out
		case 0x3e:
			return ioport("INPUTS")->read();    //TODO : trace f564
		case 0x3d:
			return ioport("A005")->read();
		default:
			logerror("A000 read with mux=0x%02x\n", m_reg_a002 & 0x3f);
			return 0xff;
	}
}

WRITE8_MEMBER(witch_state::write_a002)
{
	//A002 bit 7&6 = m_bank ????
	m_reg_a002 = data;

	m_mainbank->set_entry((data >> 6) & 3);
}

WRITE8_MEMBER(keirinou_state::write_keirinou_a002)
{
	uint8_t new_bg_bank;
	m_reg_a002 = data;

	m_spr_bank = BIT(data,7);
	new_bg_bank = BIT(data,6);

	if(m_bg_bank != new_bg_bank)
	{
		m_bg_bank = new_bg_bank;
		m_gfx1_tilemap->mark_all_dirty();
	}
//  m_mainbank->set_entry((data >> 6) & 3);
}

WRITE8_MEMBER(witch_state::write_a006)
{
	// don't write when zeroed on reset
	if (data == 0)
		return;

	m_hopper->motor_w(!BIT(data, 1) ^ m_motor_active);

	// TODO: Bit 3 = Attendant Pay

	machine().bookkeeping().coin_counter_w(2, !BIT(data, 5)); // key in counter
	machine().bookkeeping().coin_counter_w(0, !BIT(data, 6)); // coin in counter
}

WRITE8_MEMBER(witch_state::main_write_a008)
{
	m_maincpu->set_input_line(0, CLEAR_LINE);
}

WRITE8_MEMBER(witch_state::sub_write_a008)
{
	m_subcpu->set_input_line(0, CLEAR_LINE);
}

READ8_MEMBER(witch_state::prot_read_700x)
{
/*
    Code @$21a looks like simple protection check.

    - write 7,6,0 to $700f
    - read 5 bytes from $7000-$7004 ( bit 1 of $700d is data "READY" status)

    Data @ $7000 must differs from data @$7001-04.
    Otherwise later in game some I/O (controls) reads are skipped.
*/

	switch(m_subcpu->pc())
	{
	case 0x23f:
	case 0x246:
	case 0x24c:
	case 0x252:
	case 0x258:
	case 0x25e:
		return offset;//enough to pass...
	}
	return memregion("sub")->base()[0x7000+offset];
}

WRITE8_MEMBER(witch_state::xscroll_w)
{
	m_scrollx = data;
	// need to mark tiles dirty here, as the tilemap writes are affected by scrollx, see FIX_OFFSET macro.
	// without it keirin ou can seldomly draw garbage after a big/small bonus game
	// TODO: rewrite tilemap code so that it doesn't need FIX_OFFSET at all!
	m_gfx1_tilemap->mark_all_dirty();
}

WRITE8_MEMBER(witch_state::yscroll_w)
{
	m_scrolly = data;
}

WRITE8_MEMBER(keirinou_state::palette_w)
{
	int r,g,b;

	m_paletteram[offset] = data;

	// TODO: bit 0?
	r = ((m_paletteram[offset] & 0xe)>>1);
	g = ((m_paletteram[offset] & 0x30)>>4);
	b = ((m_paletteram[offset] & 0xc0)>>6);

	m_palette->set_pen_color(offset, pal3bit(r), pal2bit(g), pal2bit(b));

	// sprite palette uses an indirect table
	// sprites are only used to draw cyclists, and pens 0x01-0x05 are directly tied to a specific sprite entry.
	// this probably translates in HW by selecting a specific pen for the lowest bit in GFX roms instead of the typical color entry offset.

	// we do some massaging of the data here, the alternative is to use custom drawing code but that quite doesn't fit with witch
	if((offset & 0x1f0) == 0x00)
	{
		int i;

		if(offset > 5)
		{
			for(i=0;i<0x80;i+=0x10)
				m_palette->set_pen_color(offset+0x200+i, pal3bit(r), pal2bit(g), pal2bit(b));
		}
		else
		{
			for(i=(offset & 7) * 0x10;i<((offset & 7) * 0x10)+8;i++)
				m_palette->set_pen_color(0x200+i, pal3bit(r), pal2bit(g), pal2bit(b));
		}
	}
}

/**********************************************
 *
 * Base address map
 *
 *********************************************/

void witch_state::common_map(address_map &map)
{
	map(0xa000, 0xa003).rw(m_ppi[0], FUNC(i8255_device::read), FUNC(i8255_device::write));
	map(0xa004, 0xa007).rw(m_ppi[1], FUNC(i8255_device::read), FUNC(i8255_device::write));
	map(0xa00c, 0xa00c).portr("SERVICE");    // stats / reset
	map(0xa00e, 0xa00e).portr("COINS");      // coins/attendant keys
	map(0xc000, 0xc3ff).ram().w(FUNC(witch_state::gfx0_vram_w)).share("gfx0_vram");
	map(0xc400, 0xc7ff).ram().w(FUNC(witch_state::gfx0_cram_w)).share("gfx0_cram");
	map(0xc800, 0xcbff).rw(FUNC(witch_state::gfx1_vram_r), FUNC(witch_state::gfx1_vram_w)).share("gfx1_vram");
	map(0xcc00, 0xcfff).rw(FUNC(witch_state::gfx1_cram_r), FUNC(witch_state::gfx1_cram_w)).share("gfx1_cram");
}

/************************************
 *
 * Witch address maps
 *
 ***********************************/

void witch_state::witch_common_map(address_map &map)
{
	common_map(map);
	map(0x8000, 0x8001).rw("ym1", FUNC(ym2203_device::read), FUNC(ym2203_device::write));
	map(0x8008, 0x8009).rw("ym2", FUNC(ym2203_device::read), FUNC(ym2203_device::write));
	map(0x8010, 0x8016).rw("essnd", FUNC(es8712_device::read), FUNC(es8712_device::write));
	map(0xd000, 0xdfff).ram().share("sprite_ram");
	map(0xe000, 0xe7ff).ram().w(m_palette, FUNC(palette_device::write8)).share("palette");
	map(0xe800, 0xefff).ram().w(m_palette, FUNC(palette_device::write8_ext)).share("palette_ext");
	map(0xf000, 0xf0ff).ram().share("share1");
	map(0xf100, 0xf1ff).ram().share("nvram");
	map(0xf200, 0xffff).ram().share("share2");
}

void witch_state::witch_main_map(address_map &map)
{
	witch_common_map(map);
	map(0x0000, UNBANKED_SIZE-1).rom();
	map(UNBANKED_SIZE, 0x7fff).bankr("mainbank");
	map(0xa008, 0xa008).w(FUNC(witch_state::main_write_a008));
}


void witch_state::witch_sub_map(address_map &map)
{
	witch_common_map(map);
	map(0x0000, 0x7fff).rom();
	map(0xa008, 0xa008).w(FUNC(witch_state::sub_write_a008));
}

/************************************
 *
 * Keirin Ou address maps
 *
 ***********************************/

void keirinou_state::keirinou_common_map(address_map &map)
{
	common_map(map);
	map(0x8000, 0x8001).rw("ay1", FUNC(ay8910_device::data_r), FUNC(ay8910_device::address_data_w));
	map(0x8002, 0x8003).rw("ay2", FUNC(ay8910_device::data_r), FUNC(ay8910_device::address_data_w));
	map(0xd000, 0xd7ff).ram().share("sprite_ram");
	map(0xd800, 0xd9ff).ram().w(FUNC(keirinou_state::palette_w)).share("paletteram");
	map(0xe000, 0xe7ff).ram();
	map(0xe800, 0xefff).ram().share("nvram"); // shared with sub
}

void keirinou_state::keirinou_main_map(address_map &map)
{
	keirinou_common_map(map);
	map(0x0000, 0x7fff).rom();
	map(0xa008, 0xa008).w(FUNC(witch_state::main_write_a008));
}

void keirinou_state::keirinou_sub_map(address_map &map)
{
	keirinou_common_map(map);
	map(0x0000, 0x7fff).rom();
	map(0xa008, 0xa008).w(FUNC(witch_state::sub_write_a008));
}

static INPUT_PORTS_START( witch )
	PORT_START("SERVICE")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_CUSTOM ) PORT_READ_LINE_DEVICE_MEMBER("hopper", ticket_dispenser_device, line_r)
	PORT_BIT( 0x06, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_MEMORY_RESET )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_GAMBLE_BOOK )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("COINS")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_GAMBLE_KEYIN )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_GAMBLE_KEYOUT ) PORT_NAME("Attendant Pay")
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("UNK")   /* Not a DSW */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_OTHER ) PORT_NAME("Payout 2")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_GAMBLE_PAYOUT )
	PORT_BIT( 0xfc, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("INPUTS")    /* Inputs */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_NAME("Left Flipper")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_GAMBLE_HIGH ) PORT_NAME("Big")
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_GAMBLE_LOW ) PORT_NAME("Small")
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_GAMBLE_TAKE )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_GAMBLE_D_UP )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_GAMBLE_BET )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_NAME("Right Flipper")

/*
F180 kkkbbppp ; Read onPORT 0xA005
 ppp  = PAY OUT | 60 ; 65 ; 70 ; 75 ; 80 ; 85 ; 90 ; 95
 bb   = MAX BET | 20 ; 30 ; 40 ; 60
 kkk  = KEY IN  | 1-10 ; 1-20 ; 1-40 ; 1-50 ; 1-100 ; 1-200 ; 1-250 ; 1-500
*/
	PORT_START("A005")  /* DSW "SW2" */
	PORT_DIPNAME( 0x07, 0x07, "Pay Out" )   PORT_DIPLOCATION("SW2:1,2,3")
	PORT_DIPSETTING(    0x07, "60" )
	PORT_DIPSETTING(    0x06, "65" )
	PORT_DIPSETTING(    0x05, "70" )
	PORT_DIPSETTING(    0x04, "75" )
	PORT_DIPSETTING(    0x03, "80" )
	PORT_DIPSETTING(    0x02, "85" )
	PORT_DIPSETTING(    0x01, "90" )
	PORT_DIPSETTING(    0x00, "95" )
	PORT_DIPNAME( 0x18, 0x00, "Max Bet" )   PORT_DIPLOCATION("SW2:4,5")
	PORT_DIPSETTING(    0x18, "20" )
	PORT_DIPSETTING(    0x10, "30" )
	PORT_DIPSETTING(    0x08, "40" )
	PORT_DIPSETTING(    0x00, "60" )
	PORT_DIPNAME( 0xe0, 0xe0, "Key In" )    PORT_DIPLOCATION("SW2:6,7,8")
	PORT_DIPSETTING(    0xE0, "1-10"  )
	PORT_DIPSETTING(    0xC0, "1-20"  )
	PORT_DIPSETTING(    0xA0, "1-40"  )
	PORT_DIPSETTING(    0x80, "1-50"  )
	PORT_DIPSETTING(    0x60, "1-100" )
	PORT_DIPSETTING(    0x40, "1-200" )
	PORT_DIPSETTING(    0x20, "1-250" )
	PORT_DIPSETTING(    0x00, "1-500" )
/*
*f181   : ccccxxxd ; Read onPORT 0xA004
 d    = DOUBLE UP | ON ; OFF
 cccc = COIN IN1 | 1-1 ; 1-2 ; 1-3 ; 1-4 ; 1-5 ; 1-6 ; 1-7 ; 1-8 ; 1-9 ; 1-10 ; 1-15 ; 1-20 ; 1-25 ; 1-30 ; 1-40 ; 1-50
*/
	PORT_START("A004")  /* DSW "SW3" Switches 2-4 not defined in manual */
	PORT_DIPNAME( 0x01, 0x00, "Double Up" )     PORT_DIPLOCATION("SW3:1")
	PORT_DIPSETTING(    0x01, DEF_STR( Off  ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPUNUSED_DIPLOC( 0x02, 0x02, "SW3:2" )
	PORT_DIPUNUSED_DIPLOC( 0x04, 0x04, "SW3:3" )
	PORT_DIPUNUSED_DIPLOC( 0x08, 0x08, "SW3:4" )
	PORT_DIPNAME( 0xf0, 0xf0, "Coin In 1" )     PORT_DIPLOCATION("SW3:5,6,7,8")
	PORT_DIPSETTING(    0xf0, "1-1" )
	PORT_DIPSETTING(    0xe0, "1-2" )
	PORT_DIPSETTING(    0xd0, "1-3" )
	PORT_DIPSETTING(    0xc0, "1-4" )
	PORT_DIPSETTING(    0xb0, "1-5" )
	PORT_DIPSETTING(    0xa0, "1-6" )
	PORT_DIPSETTING(    0x90, "1-7" )
	PORT_DIPSETTING(    0x80, "1-8" )
	PORT_DIPSETTING(    0x70, "1-9" )
	PORT_DIPSETTING(    0x60, "1-10" )
	PORT_DIPSETTING(    0x50, "1-15" )
	PORT_DIPSETTING(    0x40, "1-20" )
	PORT_DIPSETTING(    0x30, "1-25" )
	PORT_DIPSETTING(    0x20, "1-30" )
	PORT_DIPSETTING(    0x10, "1-40" )
	PORT_DIPSETTING(    0x00, "1-50" )

/*
*f182   : sttpcccc ; Read onPORT A of YM2203 @ 0x8001
 cccc = COIN IN2 | 1-1 ; 1-2 ; 1-3 ; 1-4 ; 1-5 ; 1-6 ; 1-7 ; 1-8 ; 1-9 ; 1-10 ; 2-1 ; 3-1 ; 4-1 ; 5-1 ; 6-1 ; 10-1
 p    = PAYOUT SWITCH | ON ; OFF
 tt   = TIME | 40 ; 45 ; 50 ; 55
 s    = DEMO SOUND | ON ; OFF
*/
	PORT_START("YM_PortA")  /* DSW "SW4" */
	PORT_DIPNAME( 0x0f, 0x0f, "Coin In 2" )     PORT_DIPLOCATION("SW4:1,2,3,4")
	PORT_DIPSETTING(    0x0f, "1-1" )
	PORT_DIPSETTING(    0x0e, "1-2" )
	PORT_DIPSETTING(    0x0d, "1-3" )
	PORT_DIPSETTING(    0x0c, "1-4" )
	PORT_DIPSETTING(    0x0b, "1-5" )
	PORT_DIPSETTING(    0x0a, "1-6" )
	PORT_DIPSETTING(    0x09, "1-7" )
	PORT_DIPSETTING(    0x08, "1-8" )
	PORT_DIPSETTING(    0x07, "1-9" )
	PORT_DIPSETTING(    0x06, "1-10" )
	PORT_DIPSETTING(    0x05, "2-1" )
	PORT_DIPSETTING(    0x04, "3-1" )
	PORT_DIPSETTING(    0x03, "4-1" )
	PORT_DIPSETTING(    0x02, "5-1" )
	PORT_DIPSETTING(    0x01, "6-1" )
	PORT_DIPSETTING(    0x00, "10-1" )
	PORT_DIPNAME( 0x10, 0x00, "Payout Switch" ) PORT_DIPLOCATION("SW4:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off  ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x60, 0x00, "Time" )      PORT_DIPLOCATION("SW4:6,7")
	PORT_DIPSETTING(    0x60, "40" )
	PORT_DIPSETTING(    0x40, "45" )
	PORT_DIPSETTING(    0x20, "50" )
	PORT_DIPSETTING(    0x00, "55" )
	PORT_DIPNAME( 0x80, 0x00, DEF_STR( Demo_Sounds ) ) PORT_DIPLOCATION("SW4:8")
	PORT_DIPSETTING(    0x80, DEF_STR( Off  ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )

/*
*f183 : xxxxhllb ; Read onPORT B of YM2203 @ 0x8001
 b    = AUTO BET | ON ; OFF
 ll   = GAME LIMIT | 500 ; 1000 ; 5000 ; 990000
 h    = HOPPER ACTIVE | LOW ; HIGH
*/
	PORT_START("YM_PortB")  /* DSW "SW5" Switches 5, 6 & 8 undefined in manual */
	PORT_DIPNAME( 0x01, 0x01, "Auto Bet" )      PORT_DIPLOCATION("SW5:1")
	PORT_DIPSETTING(    0x01, DEF_STR( Off  ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x06, 0x06, "Game Limit" )    PORT_DIPLOCATION("SW5:2,3")
	PORT_DIPSETTING(    0x06, "500" )
	PORT_DIPSETTING(    0x04, "1000" )
	PORT_DIPSETTING(    0x02, "5000" )
	PORT_DIPSETTING(    0x00, "990000" ) /* 10000 as defined in the Excellent System version manual */
	PORT_DIPNAME( 0x08, 0x08, "Hopper Active" ) PORT_DIPLOCATION("SW5:4")
	PORT_DIPSETTING(    0x08, DEF_STR(Low) )
	PORT_DIPSETTING(    0x00, DEF_STR(High) )
	PORT_DIPUNUSED_DIPLOC( 0x10, 0x10, "SW5:5" )
	PORT_DIPUNUSED_DIPLOC( 0x20, 0x20, "SW5:6" )
	PORT_DIPNAME( 0x40, 0x00, "Unknown Use" )   PORT_DIPLOCATION("SW5:7") /* As defined in the Excellent System version manual */
	PORT_DIPSETTING(    0x40, "Matrix" )
	PORT_DIPSETTING(    0x00, "Straight (Normal)" )
	PORT_DIPUNUSED_DIPLOC( 0x80, 0x80, "SW5:8" )
INPUT_PORTS_END

static INPUT_PORTS_START( keirinou )
	PORT_INCLUDE( witch )

	PORT_MODIFY("INPUTS")    /* Inputs */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 )     PORT_NAME("1P 1-2") PORT_CODE(KEYCODE_A)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 )     PORT_NAME("1P 1-3") PORT_CODE(KEYCODE_S)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON3 )     PORT_NAME("1P 1-4") PORT_CODE(KEYCODE_D)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_BUTTON4 )     PORT_NAME("1P 1-5") PORT_CODE(KEYCODE_F)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON5 )     PORT_NAME("1P 2-3") PORT_CODE(KEYCODE_G)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_GAMBLE_TAKE )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START1 )

	PORT_MODIFY("A005")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON6 )     PORT_NAME("1P 2-4") PORT_CODE(KEYCODE_Z)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON7 )     PORT_NAME("1P 2-5") PORT_CODE(KEYCODE_X)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON8 )     PORT_NAME("1P 3-4") PORT_CODE(KEYCODE_C)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_BUTTON9 )     PORT_NAME("1P 3-5") PORT_CODE(KEYCODE_V)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON10 )    PORT_NAME("1P 4-5") PORT_CODE(KEYCODE_B)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_GAMBLE_LOW )  PORT_NAME("Small") PORT_CODE(KEYCODE_K)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_GAMBLE_HIGH ) PORT_NAME("Big") PORT_CODE(KEYCODE_J)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_GAMBLE_D_UP )

	PORT_MODIFY("COINS")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_GAMBLE_KEYIN )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_GAMBLE_KEYOUT ) PORT_NAME("Attendant Pay")
	PORT_BIT( 0xf0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_MODIFY("SERVICE")
	// bit 0: hopper
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_MEMORY_RESET )
	PORT_BIT( 0x0c, IP_ACTIVE_LOW, IPT_UNUSED )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_GAMBLE_BOOK )
	PORT_BIT( 0xe0, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_MODIFY("UNK")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_POKER_CANCEL ) PORT_NAME("Cancel All Bets")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_GAMBLE_PAYOUT )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_GAMBLE_BET ) PORT_NAME("Bet All")
	// TODO: some of those bits are mirrors of bet buttons, they seem to break game logic tho?
	PORT_BIT( 0xf8, IP_ACTIVE_LOW, IPT_UNKNOWN )

	// TODO: dipswitches
	PORT_MODIFY("YM_PortA")
	PORT_DIPNAME( 0x07, 0x07, "Game Rate" )
	PORT_DIPSETTING(    0x02, "70%" )
	PORT_DIPSETTING(    0x07, "80%" )
	PORT_DIPSETTING(    0x06, "85%" )
	PORT_DIPSETTING(    0x05, "90%" )
	PORT_DIPSETTING(    0x04, "95%" )
	PORT_DIPSETTING(    0x03, "100%" )
//  PORT_DIPSETTING(    0x01, "80%" )
//  PORT_DIPSETTING(    0x00, "90%" )
	PORT_DIPNAME( 0x08, 0x08, "Double-Up Rate" )
	PORT_DIPSETTING(    0x08, "90%" )
	PORT_DIPSETTING(    0x00, "100%" )
	PORT_DIPNAME( 0x10, 0x00, "Double-Up Game" )
	PORT_DIPSETTING(    0x10, DEF_STR( No ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Yes ) )
	PORT_DIPNAME( 0x20, 0x20, "DSWA" )
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Yes ) )
	PORT_DIPSETTING(    0x00, DEF_STR( No ) )

	PORT_MODIFY("YM_PortB")
	PORT_DIPNAME( 0x01, 0x01, "DSWB" )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )

	PORT_MODIFY("A004")
	PORT_DIPNAME( 0x01, 0x01, "DSWC" )
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
INPUT_PORTS_END


static const gfx_layout tiles8x8_layout =
{
	8,8,
	RGN_FRAC(1,2),
	4,
	{ 0, 1, 2, 3 },
	{ 0, 4, RGN_FRAC(1,2)+0, RGN_FRAC(1,2)+4, 8, 12, RGN_FRAC(1,2)+8, RGN_FRAC(1,2)+12},
	{ 0*16, 1*16, 2*16, 3*16, 4*16, 5*16, 6*16, 7*16 },
	16*8
};

static GFXDECODE_START( gfx_witch )
	GFXDECODE_ENTRY( "gfx1", 0, tiles8x8_layout, 0, 16 )
	GFXDECODE_ENTRY( "gfx2", 0, tiles8x8_layout, 0, 16 )
GFXDECODE_END

static GFXDECODE_START( gfx_keirinou )
	GFXDECODE_ENTRY( "gfx1", 0, tiles8x8_layout, 0, 16 )
	GFXDECODE_ENTRY( "gfx2", 0, tiles8x8_layout, 0, 16 )
	GFXDECODE_ENTRY( "gfx2", 0, tiles8x8_layout, 0x200, 8 )
GFXDECODE_END

void witch_state::machine_reset()
{
	// Keep track of the "Hopper Active" DSW value because the program will use it
	m_motor_active = (ioport("YM_PortB")->read() & 0x08) ? 0 : 1;
}

MACHINE_CONFIG_START(witch_state::witch)
	/* basic machine hardware */
	MCFG_DEVICE_ADD("maincpu", Z80, CPU_CLOCK)    /* 3 MHz */
	MCFG_DEVICE_PROGRAM_MAP(witch_main_map)
	MCFG_DEVICE_VBLANK_INT_DRIVER("screen", witch_state,  irq0_line_assert)

	/* 2nd z80 */
	MCFG_DEVICE_ADD("sub", Z80, CPU_CLOCK)    /* 3 MHz */
	MCFG_DEVICE_PROGRAM_MAP(witch_sub_map)
	MCFG_DEVICE_VBLANK_INT_DRIVER("screen", witch_state,  irq0_line_assert)

	MCFG_QUANTUM_TIME(attotime::from_hz(6000))

	NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0);

	TICKET_DISPENSER(config, m_hopper, attotime::from_msec(HOPPER_PULSE), TICKET_MOTOR_ACTIVE_HIGH, TICKET_STATUS_ACTIVE_HIGH);

	// 82C255 (actual chip on PCB) is equivalent to two 8255s
	I8255(config, m_ppi[0]);
	m_ppi[0]->in_pa_callback().set(FUNC(witch_state::read_a000));
	m_ppi[0]->in_pb_callback().set_ioport("UNK");
	m_ppi[0]->out_pc_callback().set(FUNC(witch_state::write_a002));

	I8255(config, m_ppi[1]);
	m_ppi[1]->in_pa_callback().set_ioport("A004");
	m_ppi[1]->in_pb_callback().set_ioport("A005");
	m_ppi[1]->out_pc_callback().set(FUNC(witch_state::write_a006));

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_REFRESH_RATE(60)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(0))
	MCFG_SCREEN_SIZE(256, 256)
	MCFG_SCREEN_VISIBLE_AREA(8, 256-1-8, 8*4, 256-8*4-1)
	MCFG_SCREEN_UPDATE_DRIVER(witch_state, screen_update_witch)
	MCFG_SCREEN_PALETTE(m_palette)

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_witch);
	PALETTE(config, m_palette).set_format(palette_device::xBGR_555, 0x800);

	/* sound hardware */
	SPEAKER(config, "mono").front_center();

	es8712_device &essnd(ES8712(config, "essnd", 0));
	essnd.msm_write_handler().set("msm", FUNC(msm5205_device::data_w));
	essnd.set_msm_tag("msm");

	msm5205_device &msm(MSM5205(config, "msm", MSM5202_CLOCK));   /* actually MSM5202 */
	msm.vck_legacy_callback().set("essnd", FUNC(es8712_device::msm_int));
	msm.set_prescaler_selector(msm5205_device::S48_4B); /* 8 kHz */
	msm.add_route(ALL_OUTPUTS, "mono", 1.0);

	ym2203_device &ym1(YM2203(config, "ym1", YM2203_CLOCK));     /* 3 MHz */
	ym1.port_a_read_callback().set_ioport("YM_PortA");
	ym1.port_b_read_callback().set_ioport("YM_PortB");
	ym1.add_route(ALL_OUTPUTS, "mono", 0.5);

	ym2203_device &ym2(YM2203(config, "ym2", YM2203_CLOCK));     /* 3 MHz */
	ym2.port_a_write_callback().set(FUNC(witch_state::xscroll_w));
	ym2.port_b_write_callback().set(FUNC(witch_state::yscroll_w));
	ym2.add_route(ALL_OUTPUTS, "mono", 0.5);
MACHINE_CONFIG_END

MACHINE_CONFIG_START(keirinou_state::keirinou)
	witch(config);

	MCFG_DEVICE_MODIFY("maincpu")
	MCFG_DEVICE_PROGRAM_MAP(keirinou_main_map)

	MCFG_DEVICE_MODIFY("sub")
	MCFG_DEVICE_PROGRAM_MAP(keirinou_sub_map)

	PALETTE(config.replace(), m_palette).set_entries(0x200+0x80);
	m_gfxdecode->set_info(gfx_keirinou);

//  MCFG_PALETTE_FORMAT(IIBBGGRR)

	// Keirin Ou does have two individual PPIs (NEC D8255AC-2)
	m_ppi[0]->out_pc_callback().set(FUNC(keirinou_state::write_keirinou_a002));

	ay8910_device &ay1(AY8910(config, "ay1", AY8910_CLOCK));
	ay1.port_a_read_callback().set_ioport("YM_PortA");
	ay1.port_b_read_callback().set_ioport("YM_PortB");
	ay1.add_route(ALL_OUTPUTS, "mono", 0.5);

	ay8910_device &ay2(AY8910(config, "ay2", AY8910_CLOCK));
	ay2.port_a_write_callback().set(FUNC(witch_state::xscroll_w));
	ay2.port_b_write_callback().set(FUNC(witch_state::yscroll_w));
	ay2.add_route(ALL_OUTPUTS, "mono", 0.5);

	MCFG_DEVICE_REMOVE("essnd")
	MCFG_DEVICE_REMOVE("msm")
	MCFG_DEVICE_REMOVE("ym1")
	MCFG_DEVICE_REMOVE("ym2")
MACHINE_CONFIG_END

ROM_START( witch )
	ROM_REGION( 0x30000, "maincpu", 0 )
	ROM_LOAD( "u_5b.u5", 0x10000, 0x20000, CRC(5c9f685a) SHA1(b75950048009ffb8c3b356592b1c69f905a1a2bd) )
	ROM_COPY( "maincpu" , 0x10000, 0x0000, 0x8000 )

	ROM_REGION( 0x10000, "sub", 0 )
	ROM_LOAD( "6.s6", 0x00000, 0x08000, CRC(82460b82) SHA1(d85a9d77edaa67dfab8ff6ac4cb6273f0904b3c0) )

	ROM_REGION( 0x20000, "gfx1", 0 )
	ROM_LOAD( "3.u3", 0x00000, 0x20000,  CRC(7007ced4) SHA1(6a0aac3ff9a4d5360c8ba1142f010add1b430ada) )

	ROM_REGION( 0x40000, "gfx2", 0 )
	ROM_LOAD( "5.a1", 0x00000, 0x40000,  CRC(fc37a9c2) SHA1(940d8c53d47eaa93a85a91e4ecb92fc4912d331d) )

	ROM_REGION( 0x40000, "essnd", 0 )
	ROM_LOAD( "1.v10", 0x00000, 0x40000, CRC(62e42371) SHA1(5042abc2176d0c35fd6b698eca4145f93b0a3944) )

	ROM_REGION( 0x100, "prom", 0 )
	ROM_LOAD( "tbp24s10n.10k", 0x000, 0x100, CRC(ee7b9d8f) SHA1(3a7b75befab83bc37e4e403ad3632841c2d37707) ) /* Currently unused, unknown use */
ROM_END


/* Witch (With ranking) */
ROM_START( witchb )
	ROM_REGION( 0x30000, "maincpu", 0 )
	ROM_LOAD( "x.u5", 0x10000, 0x20000, CRC(d0818777) SHA1(a6232fef84bec3cfb4a6122a48e96e7b7950e013) )
	ROM_COPY( "maincpu" , 0x10000, 0x0000, 0x8000 )

	ROM_REGION( 0x10000, "sub", 0 )
	ROM_LOAD( "6.s6", 0x00000, 0x08000, CRC(82460b82) SHA1(d85a9d77edaa67dfab8ff6ac4cb6273f0904b3c0) )

	ROM_REGION( 0x20000, "gfx1", 0 )
	ROM_LOAD( "3.u3", 0x00000, 0x20000,  CRC(7007ced4) SHA1(6a0aac3ff9a4d5360c8ba1142f010add1b430ada) )

	ROM_REGION( 0x40000, "gfx2", 0 )
	ROM_LOAD( "5.a1", 0x00000, 0x40000,  CRC(fc37a9c2) SHA1(940d8c53d47eaa93a85a91e4ecb92fc4912d331d) )

	ROM_REGION( 0x40000, "essnd", 0 )
	ROM_LOAD( "1.v10", 0x00000, 0x40000, CRC(62e42371) SHA1(5042abc2176d0c35fd6b698eca4145f93b0a3944) )

	ROM_REGION( 0x100, "prom", 0 )
	ROM_LOAD( "tbp24s10n.10k", 0x000, 0x100, CRC(ee7b9d8f) SHA1(3a7b75befab83bc37e4e403ad3632841c2d37707) ) /* Currently unused, unknown use */
ROM_END


ROM_START( witchs ) /* this set has (c)1992 Sega / Vic Tokai in the roms */
	ROM_REGION( 0x30000, "maincpu", 0 )
	ROM_LOAD( "rom.u5", 0x10000, 0x20000, CRC(348fccb8) SHA1(947defd86c4a597fbfb9327eec4903aa779b3788) )
	ROM_COPY( "maincpu" , 0x10000, 0x0000, 0x8000 )

	ROM_REGION( 0x10000, "sub", 0 )
	ROM_LOAD( "6.s6", 0x00000, 0x08000, CRC(82460b82) SHA1(d85a9d77edaa67dfab8ff6ac4cb6273f0904b3c0) ) /* Same data as the Witch set */

	ROM_REGION( 0x20000, "gfx1", 0 )
	ROM_LOAD( "3.u3", 0x00000, 0x20000,  CRC(7007ced4) SHA1(6a0aac3ff9a4d5360c8ba1142f010add1b430ada) ) /* Same data as the Witch set */

	ROM_REGION( 0x40000, "gfx2", 0 )
	ROM_LOAD( "rom.a1", 0x00000, 0x40000,  CRC(512300a5) SHA1(1e9ba58d1ddbfb8276c68f6d5c3591e6b77abf21) )

	ROM_REGION( 0x40000, "essnd", 0 )
	ROM_LOAD( "1.v10", 0x00000, 0x40000, CRC(62e42371) SHA1(5042abc2176d0c35fd6b698eca4145f93b0a3944) ) /* Same data as the Witch set */

	ROM_REGION( 0x100, "prom", 0 )
	ROM_LOAD( "tbp24s10n.10k", 0x000, 0x100, CRC(ee7b9d8f) SHA1(3a7b75befab83bc37e4e403ad3632841c2d37707) ) /* Currently unused, unknown use */
ROM_END


ROM_START( pbchmp95 ) /* Licensed for Germany? */
	ROM_REGION( 0x30000, "maincpu", 0 )
	ROM_LOAD( "3.bin", 0x10000, 0x20000, CRC(e881aa05) SHA1(10d259396cac4b9a1b72c262c11ffa5efbdac433) )
	ROM_COPY( "maincpu" , 0x10000, 0x0000, 0x8000 )

	ROM_REGION( 0x10000, "sub", 0 )
	ROM_LOAD( "4.bin", 0x00000, 0x08000, CRC(82460b82) SHA1(d85a9d77edaa67dfab8ff6ac4cb6273f0904b3c0) ) /* Same data as the Witch set */

	ROM_REGION( 0x20000, "gfx1", 0 )
	ROM_LOAD( "2.bin", 0x00000, 0x20000,  CRC(7007ced4) SHA1(6a0aac3ff9a4d5360c8ba1142f010add1b430ada) ) /* Same data as the Witch set */

	ROM_REGION( 0x40000, "gfx2", 0 )
	ROM_LOAD( "1.bin", 0x00000, 0x40000,  CRC(f6cf7ed6) SHA1(327580a17eb2740fad974a01d97dad0a4bef9881) )

	ROM_REGION( 0x40000, "essnd", 0 )
	ROM_LOAD( "5.bin", 0x00000, 0x40000, CRC(62e42371) SHA1(5042abc2176d0c35fd6b698eca4145f93b0a3944) ) /* Same data as the Witch set */

	ROM_REGION( 0x100, "prom", 0 )
	ROM_LOAD( "tbp24s10n.10k", 0x000, 0x100, CRC(ee7b9d8f) SHA1(3a7b75befab83bc37e4e403ad3632841c2d37707) ) /* Currently unused, unknown use */
ROM_END

ROM_START( keirinou ) /* ES8611 PCB */
	ROM_REGION( 0x10000, "maincpu", ROMREGION_ERASE00 )
	ROM_LOAD( "y5-03.y5",     0x000000, 0x008000, CRC(df2acc37) SHA1(9ad953843ba7859a55888fb87591cc8d322136ad) )

	ROM_REGION( 0x10000, "sub", ROMREGION_ERASE00 )
	ROM_LOAD( "y8.y8",        0x000000, 0x008000, CRC(b34111ac) SHA1(4ed7229846adbb27695bf3dd532247b1f8f6e83e) )

	// rearranged so that it fits available gfx decode
	ROM_REGION( 0x10000, "gfx1", ROMREGION_ERASE00 )
	ROM_LOAD( "a6.a6",        0x0000, 0x4000, CRC(6d59a5e4) SHA1(4580756ee7db4a088ad02cd56f78fd55fef6ec0a) )
	ROM_CONTINUE(             0x8000, 0x4000 )
	ROM_LOAD( "c6-02.c6",     0x4000, 0x4000, CRC(c3ecc620) SHA1(9d5e18acef2ad48b8f1c4ed5bb002bb48ab6e7a7) )
	ROM_CONTINUE(             0xc000, 0x4000 )

	ROM_REGION( 0x10000, "gfx2", ROMREGION_ERASE00 )
	ROM_LOAD( "k5.k5",        0x0000, 0x04000, CRC(1ba6d1c0) SHA1(95203af518c52d731969086e326c9335dee8c465) )
	ROM_CONTINUE(             0x8000, 0x04000 )
	ROM_CONTINUE(             0x4000, 0x04000 )
	ROM_CONTINUE(             0xc000, 0x04000 )

	ROM_REGION( 0x100, "prom", 0 ) /* Same data as the Witch set, currently unused, unknown use */
	ROM_LOAD( "n82s129an.r8",      0x000000, 0x000100, CRC(ee7b9d8f) SHA1(3a7b75befab83bc37e4e403ad3632841c2d37707) ) /* N82S129AN BPROM stamped  K  */
ROM_END

void witch_state::init_witch()
{
	m_mainbank->configure_entries(0, 4, memregion("maincpu")->base() + 0x10000 + UNBANKED_SIZE, 0x8000);
	m_mainbank->set_entry(0);

	m_subcpu->space(AS_PROGRAM).install_read_handler(0x7000, 0x700f, read8_delegate(FUNC(witch_state::prot_read_700x), this));
}

GAME( 1987, keirinou, 0,     keirinou, keirinou, keirinou_state, empty_init, ROT0, "Excellent System", "Keirin Ou", MACHINE_SUPPORTS_SAVE )
GAME( 1992, witch,    0,     witch,    witch,    witch_state,    init_witch, ROT0, "Vic Tokai (Excellent System license)", "Witch", MACHINE_SUPPORTS_SAVE )
GAME( 1992, witchb,   witch, witch,    witch,    witch_state,    init_witch, ROT0, "Vic Tokai (Excellent System license)", "Witch (with ranking)", MACHINE_SUPPORTS_SAVE )
GAME( 1992, witchs,   witch, witch,    witch,    witch_state,    init_witch, ROT0, "Vic Tokai (Sega license)", "Witch (Sega license)", MACHINE_SUPPORTS_SAVE )
GAME( 1995, pbchmp95, witch, witch,    witch,    witch_state,    init_witch, ROT0, "Veltmeijer Automaten", "Pinball Champ '95", MACHINE_SUPPORTS_SAVE )
