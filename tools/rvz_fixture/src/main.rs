//! Writes synthetic Wii disc images in Dolphin's RVZ format (its docs/WiaAndRvz.md) and the game partition data each
//! one holds (what game.pak would be), then reads every image back with nod, an independent reader, and checks that
//! it returns the same data. Every byte is made here: the disc is a fake with the structure the readers look at.
//!
//!   cargo run --release --manifest-path tools/rvz_fixture/Cargo.toml -- <output directory>
//!
//! Each variant `<name>` gives `<name>.rvz` and `<name>.pak`. The partition is laid out like web/tests/make_fixture.py's
//! fixture_emptydol.pak (RMGE01, an empty main.dol, a file table with only the root), with pseudorandom data around
//! it, a group of zeros, RVZ packing of pseudorandom padding (also across a group boundary), hash exception lists, and
//! groups stored uncompressed among compressed ones.

use sha1::{Digest, Sha1};
use std::fs;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

const SECTOR: u64 = 0x8000;
const SECTOR_DATA: u64 = 0x7C00;
const PARTITION: u64 = 0x50000; // the game partition's header on the disc
const DATA_OFFSET: u64 = 0x20000; // its data, from the partition's start
const SECTORS: u64 = 160; // its data: 160 sectors, 0x4D8000 bytes without hashes
const SPLIT: u64 = 64; // pd[0] (boot .. file table) and pd[1], split at 2 MiB of disc data

struct Variant {
    name: &'static str,
    chunk: u64,
    zstd: bool,
}

const VARIANTS: [Variant; 3] = [
    Variant { name: "rvz_zstd_128k", chunk: 0x20000, zstd: true },
    Variant { name: "rvz_zstd_2m", chunk: 0x200000, zstd: true },
    Variant { name: "rvz_none_128k", chunk: 0x20000, zstd: false },
];

struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
    fn fill(&mut self, out: &mut [u8]) {
        for byte in out {
            *byte = self.next() as u8;
        }
    }
}

/// The discs' padding generator (RVZ packing): a lagged Fibonacci generator, f = xor, j = 32, k = 521.
struct Lfg {
    words: [u32; 521],
    position: usize,
}

impl Lfg {
    fn new(seed: &[u8; 68]) -> Lfg {
        let mut words = [0u32; 521];
        for i in 0..17 {
            words[i] = u32::from_be_bytes(seed[i * 4..i * 4 + 4].try_into().unwrap());
        }
        for i in 17..521 {
            words[i] = (words[i - 17] << 23) ^ (words[i - 16] >> 9) ^ words[i - 1];
        }
        let mut lfg = Lfg { words, position: 0 };
        for _ in 0..4 {
            lfg.advance();
        }
        lfg
    }
    fn advance(&mut self) {
        for i in 0..32 {
            self.words[i] ^= self.words[i + 521 - 32];
        }
        for i in 32..521 {
            self.words[i] ^= self.words[i - 32];
        }
    }
    fn byte(&mut self) -> u8 {
        let word = self.words[self.position / 4];
        let byte = (word >> [24, 18, 8, 0][self.position % 4]) as u8;
        self.position += 1;
        if self.position == 521 * 4 {
            self.advance();
            self.position = 0;
        }
        byte
    }
    /// `len` bytes of padding at `offset` in the generator's frame (restarted every 0x8000 bytes).
    fn bytes(seed: &[u8; 68], offset: u64, len: usize) -> Vec<u8> {
        let mut lfg = Lfg::new(seed);
        for _ in 0..offset % SECTOR {
            lfg.byte();
        }
        (0..len).map(|_| lfg.byte()).collect()
    }
}

/// Padding in the partition's data: [start, end) inside one 0x8000-byte window, from `seed`.
struct Padding {
    start: u64,
    end: u64,
    seed: [u8; 68],
}

fn put_u32(out: &mut [u8], at: usize, value: u32) {
    out[at..at + 4].copy_from_slice(&value.to_be_bytes());
}

fn put_u64(out: &mut [u8], at: usize, value: u64) {
    out[at..at + 8].copy_from_slice(&value.to_be_bytes());
}

/// The game partition's data: what game.pak holds.
fn partition_data(rng: &mut Rng, groups_data: u64) -> (Vec<u8>, Vec<Padding>) {
    let size = (SECTORS * SECTOR_DATA) as usize;
    let mut data = vec![0u8; size];
    rng.fill(&mut data);
    // boot.bin: the game ID, the Wii magic, a title; main.dol at 0x20000 (an empty header), the file table at
    // 0x30000 (the root, no files), with their offsets divided by 4 as Wii partitions store them
    data[..0x440].fill(0);
    data[..6].copy_from_slice(b"RMGE01");
    put_u32(&mut data, 0x18, 0x5D1C_9EA3);
    data[0x20..0x20 + 25].copy_from_slice(b"SYNTHETIC RVZ TEST (FAKE)");
    put_u32(&mut data, 0x420, 0x20000 >> 2);
    put_u32(&mut data, 0x424, 0x30000 >> 2);
    put_u32(&mut data, 0x428, 0x10 >> 2);
    data[0x20000..0x20100].fill(0);
    data[0x30000..0x30010].fill(0);
    data[0x30000] = 1; // a directory
    put_u32(&mut data, 0x30008, 1); // the root: one entry
    // A whole group of zeros (the format's "all zero" group): pd[1]'s third group, when there is one
    let zero_group = SPLIT * SECTOR_DATA + 2 * groups_data;
    if zero_group < size as u64 {
        let zero_end = (zero_group + groups_data).min(size as u64);
        data[zero_group as usize..zero_end as usize].fill(0);
    }
    // Padding: one inside a group, one across the boundary between two groups, one at a window's start
    let mut paddings = Vec::new();
    let boundary = (SPLIT * SECTOR_DATA) + groups_data; // a group boundary inside pd[1]
    let window = boundary / SECTOR * SECTOR;
    for (start, end) in [
        (0x41000, 0x45800),
        (boundary.max(window + 0x100) - 0x80, (boundary + 0x900).min(window + SECTOR)),
        (0x3A0000, 0x3A0000 + 0x2000),
    ] {
        assert!(start / SECTOR == (end - 1) / SECTOR, "padding must stay in one 0x8000-byte window");
        let mut seed = [0u8; 68];
        rng.fill(&mut seed);
        let bytes = Lfg::bytes(&seed, start, (end - start) as usize);
        data[start as usize..end as usize].copy_from_slice(&bytes);
        paddings.push(Padding { start, end, seed });
    }
    (data, paddings)
}

/// The disc's bytes before the partition's data: disc header, partition table, partition header.
fn disc_head(rng: &mut Rng) -> Vec<u8> {
    let mut disc = vec![0u8; (PARTITION + DATA_OFFSET) as usize];
    disc[..6].copy_from_slice(b"RMGE01");
    put_u32(&mut disc, 0x18, 0x5D1C_9EA3);
    disc[0x20..0x20 + 25].copy_from_slice(b"SYNTHETIC RVZ TEST (FAKE)");
    // Partition table: group 0 holds one partition, type 0 (the game)
    put_u32(&mut disc, 0x40000, 1);
    put_u32(&mut disc, 0x40004, (0x40020 >> 2) as u32);
    put_u32(&mut disc, 0x40020, (PARTITION >> 2) as u32);
    put_u32(&mut disc, 0x40024, 0);
    // Partition header: a ticket with the retail issuer and a made-up title key, a TMD of zeros, no certificates,
    // an H3 table of zeros, and where the data is
    let p = PARTITION as usize;
    put_u32(&mut disc, p, 0x0001_0001);
    disc[p + 0x140..p + 0x140 + 26].copy_from_slice(b"Root-CA00000001-XS00000003");
    rng.fill(&mut disc[p + 0x1BF..p + 0x1CF]);
    disc[p + 0x1DC..p + 0x1E4].copy_from_slice(&[0, 1, 0, 0, b'R', b'M', b'G', b'E']);
    put_u32(&mut disc, p + 0x2A4, 0x208);
    put_u32(&mut disc, p + 0x2A8, (0x2C0 >> 2) as u32);
    put_u32(&mut disc, p + 0x2AC, 0);
    put_u32(&mut disc, p + 0x2B0, 0);
    put_u32(&mut disc, p + 0x2B4, (0x8000 >> 2) as u32);
    put_u32(&mut disc, p + 0x2B8, (DATA_OFFSET >> 2) as u32);
    put_u32(&mut disc, p + 0x2BC, ((SECTORS * SECTOR) >> 2) as u32);
    put_u32(&mut disc, p + 0x2C0, 0x0001_0001);
    disc
}

struct Writer {
    zstd: bool,
    blob: Vec<u8>, // group data, from file offset `base`
    base: u64,
    groups: Vec<[u32; 3]>,
    stored: usize, // groups kept uncompressed (most significant bit of data_size clear)
}

impl Writer {
    /// Adds a group: `lists` hash exception lists (partition data) then `data`, RVZ-packed when `paddings` touch it.
    fn group(&mut self, data: &[u8], offset: u64, lists: Option<Vec<Vec<u8>>>, paddings: &[Padding]) {
        let index = self.groups.len();
        let no_exceptions = lists.as_ref().is_none_or(|l| l.iter().all(|e| e == &[0, 0]));
        if no_exceptions && data.iter().all(|&b| b == 0) {
            self.groups.push([(self.position() / 4) as u32, 0, 0]);
            return;
        }
        let mut payload = Vec::new();
        let end = offset + data.len() as u64;
        let touching: Vec<&Padding> = paddings.iter().filter(|p| p.start < end && p.end > offset).collect();
        let packed = !touching.is_empty();
        if packed {
            let mut at = offset;
            for padding in touching {
                let start = padding.start.max(offset);
                let stop = padding.end.min(end);
                if start > at {
                    payload.extend_from_slice(&((start - at) as u32).to_be_bytes());
                    payload.extend_from_slice(&data[(at - offset) as usize..(start - offset) as usize]);
                }
                payload.extend_from_slice(&(((stop - start) as u32) | 0x8000_0000).to_be_bytes());
                payload.extend_from_slice(&padding.seed);
                at = stop;
            }
            if at < end {
                payload.extend_from_slice(&((end - at) as u32).to_be_bytes());
                payload.extend_from_slice(&data[(at - offset) as usize..]);
            }
        } else {
            payload.extend_from_slice(data);
        }
        let compressed = self.zstd && index % 5 != 3; // some groups stay uncompressed in a compressed image
        let mut stream = Vec::new();
        for list in lists.iter().flatten() {
            stream.extend_from_slice(list);
        }
        if !compressed {
            while stream.len() % 4 != 0 {
                stream.push(0); // stored as is: the data starts on a 4-byte boundary
            }
        }
        stream.extend_from_slice(&payload);
        let stored = if compressed { zstd::encode_all(&stream[..], 5).unwrap() } else { stream };
        if !compressed && self.zstd {
            self.stored += 1;
        }
        let position = self.position();
        let size = stored.len() as u32 | if compressed { 0x8000_0000 } else { 0 };
        self.groups.push([(position / 4) as u32, size, if packed { payload.len() as u32 } else { 0 }]);
        self.blob.extend_from_slice(&stored);
        while self.blob.len() % 4 != 0 {
            self.blob.push(0);
        }
    }
    fn position(&self) -> u64 {
        self.base + self.blob.len() as u64
    }
    fn table(&self, bytes: &[u8]) -> Vec<u8> {
        if self.zstd { zstd::encode_all(bytes, 5).unwrap() } else { bytes.to_vec() }
    }
}

fn write_rvz(variant: &Variant, out: &Path) -> Vec<u8> {
    let mut rng = Rng(0x5EED_0000_0000_0001 ^ variant.chunk ^ variant.zstd as u64);
    let groups_data = variant.chunk / SECTOR * SECTOR_DATA;
    let (data, paddings) = partition_data(&mut rng, groups_data);
    let disc = disc_head(&mut rng);
    let lists_per_group = (variant.chunk / 0x200000).max(1) as usize;

    const HEADER: u64 = 0x48 + 0xDC + 0x30;
    let mut writer = Writer { zstd: variant.zstd, blob: Vec::new(), base: HEADER, groups: Vec::new(), stored: 0 };
    // Raw data: the disc before the partition's data, in chunks from 0 (the struct says 0x80, as the format requires)
    let raw_first = writer.groups.len() as u32;
    for start in (0..disc.len()).step_by(variant.chunk as usize) {
        let end = (start + variant.chunk as usize).min(disc.len());
        writer.group(&disc[start..end], start as u64, None, &[]);
    }
    let raw_groups = writer.groups.len() as u32 - raw_first;
    // Partition data, in its two segments
    let mut pd = Vec::new();
    for (first, sectors) in [(0, SPLIT), (SPLIT, SECTORS - SPLIT)] {
        let index = writer.groups.len() as u32;
        let start = first * SECTOR_DATA;
        let end = (first + sectors) * SECTOR_DATA;
        let mut offset = start;
        while offset < end {
            let stop = (offset + groups_data).min(end);
            let mut lists = vec![vec![0u8, 0]; lists_per_group];
            if writer.groups.len() % 7 == 1 {
                // a list with two exceptions (offset, then a made-up 20-byte hash)
                let mut list = vec![0u8, 2];
                for k in 0..2u16 {
                    list.extend_from_slice(&(k * 0x14).to_be_bytes());
                    let mut hash = [0u8; 20];
                    rng.fill(&mut hash);
                    list.extend_from_slice(&hash);
                }
                lists[0] = list;
            }
            writer.group(&data[offset as usize..stop as usize], offset, Some(lists), &paddings);
            offset = stop;
        }
        let first_sector = ((PARTITION + DATA_OFFSET) / SECTOR + first) as u32;
        pd.push([first_sector, sectors as u32, index, writer.groups.len() as u32 - index]);
    }

    // The tables after the group data
    let mut part = vec![0u8; 0x30];
    rng.fill(&mut part[..16]);
    for (s, entry) in pd.iter().enumerate() {
        for (k, value) in entry.iter().enumerate() {
            put_u32(&mut part, 16 + s * 16 + k * 4, *value);
        }
    }
    let mut raw = vec![0u8; 24];
    put_u64(&mut raw, 0, 0x80);
    put_u64(&mut raw, 8, disc.len() as u64 - 0x80);
    put_u32(&mut raw, 16, raw_first);
    put_u32(&mut raw, 20, raw_groups);
    let raw_table = writer.table(&raw);
    let mut group_bytes = Vec::new();
    for group in &writer.groups {
        for value in group {
            group_bytes.extend_from_slice(&value.to_be_bytes());
        }
    }
    let group_table = writer.table(&group_bytes);
    let raw_off = writer.position();
    let group_off = raw_off + raw_table.len() as u64;
    let file_size = group_off + group_table.len() as u64;

    let mut disc_struct = vec![0u8; 0xDC];
    put_u32(&mut disc_struct, 0x00, 2); // Wii
    put_u32(&mut disc_struct, 0x04, if variant.zstd { 5 } else { 0 });
    put_u32(&mut disc_struct, 0x08, 5);
    put_u32(&mut disc_struct, 0x0C, variant.chunk as u32);
    disc_struct[0x10..0x90].copy_from_slice(&disc[..0x80]);
    put_u32(&mut disc_struct, 0x90, 1);
    put_u32(&mut disc_struct, 0x94, 0x30);
    put_u64(&mut disc_struct, 0x98, 0x48 + 0xDC);
    disc_struct[0xA0..0xB4].copy_from_slice(&Sha1::digest(&part));
    put_u32(&mut disc_struct, 0xB4, 1);
    put_u64(&mut disc_struct, 0xB8, raw_off);
    put_u32(&mut disc_struct, 0xC0, raw_table.len() as u32);
    put_u32(&mut disc_struct, 0xC4, writer.groups.len() as u32);
    put_u64(&mut disc_struct, 0xC8, group_off);
    put_u32(&mut disc_struct, 0xD0, group_table.len() as u32);

    let mut head = vec![0u8; 0x48];
    head[..4].copy_from_slice(b"RVZ\x01");
    put_u32(&mut head, 4, 0x0100_0000);
    put_u32(&mut head, 8, 0x0003_0000);
    put_u32(&mut head, 12, 0xDC);
    head[0x10..0x24].copy_from_slice(&Sha1::digest(&disc_struct));
    put_u64(&mut head, 0x24, PARTITION + DATA_OFFSET + SECTORS * SECTOR);
    put_u64(&mut head, 0x2C, file_size);
    let head_hash = Sha1::digest(&head[..0x34]);
    head[0x34..0x48].copy_from_slice(&head_hash);

    let mut file = Vec::new();
    file.extend_from_slice(&head);
    file.extend_from_slice(&disc_struct);
    file.extend_from_slice(&part);
    file.extend_from_slice(&writer.blob);
    file.extend_from_slice(&raw_table);
    file.extend_from_slice(&group_table);
    assert_eq!(file.len() as u64, file_size);
    fs::write(out.join(format!("{}.rvz", variant.name)), &file).unwrap();
    fs::write(out.join(format!("{}.pak", variant.name)), &data).unwrap();
    println!(
        "{}: {} bytes, {} groups ({} stored uncompressed), partition data {} bytes",
        variant.name,
        file.len(),
        writer.groups.len(),
        writer.stored,
        data.len()
    );
    data
}

/// nod's view of the image's game partition, which must be `expected`.
fn check_with_nod(path: &Path, expected: &[u8]) -> Result<(), String> {
    let disc = nod::Disc::new(path).map_err(|e| format!("nod cannot open it: {e}"))?;
    let mut partition =
        disc.open_partition_kind(nod::PartitionKind::Data).map_err(|e| format!("nod has no game partition: {e}"))?;
    partition.seek(SeekFrom::Start(0)).map_err(|e| e.to_string())?;
    let mut actual = vec![0u8; expected.len()];
    partition.read_exact(&mut actual).map_err(|e| format!("nod cannot read the partition: {e}"))?;
    if let Some(at) = actual.iter().zip(expected).position(|(a, b)| a != b) {
        return Err(format!("nod reads 0x{:02x} at 0x{at:x}, the image holds 0x{:02x}", actual[at], expected[at]));
    }
    Ok(())
}

fn main() {
    let out = PathBuf::from(std::env::args().nth(1).expect("usage: rvz_fixture <output directory>"));
    fs::create_dir_all(&out).unwrap();
    let mut failed = false;
    for variant in &VARIANTS {
        let expected = write_rvz(variant, &out);
        match check_with_nod(&out.join(format!("{}.rvz", variant.name)), &expected) {
            Ok(()) => {
                println!("PASS nod reads {}'s game partition byte for byte", variant.name);
                // The comparison catches a single wrong byte: one inside the first padding run
                let mut altered = expected.clone();
                altered[0x41000 + 0x123] ^= 0x01;
                if check_with_nod(&out.join(format!("{}.rvz", variant.name)), &altered).is_ok() {
                    println!("FAIL {}: the comparison missed a changed byte", variant.name);
                    failed = true;
                }
            }
            Err(error) => {
                println!("FAIL {}: {error}", variant.name);
                failed = true;
            }
        }
    }
    std::process::exit(failed as i32);
}
