/**
 * Object blocks: more than 256 files in one user area, and the API that grants them.
 *
 * A SINTRAN user area holds 256 files per object block, one by default and up to
 * 16 (4096 files) on version K. Every implementation here assumed 256 was a hard
 * limit until 2026-08-01; these tests cover the structure that assumption hid,
 * and the `@GIVE-OBJECT-BLOCKS` equivalent that raises the ceiling.
 *
 * Ground truth was measured on a live SINTRAN III K pack - see
 * docs/NDFS-OBJECT-BLOCKS-SPEC.md.
 */
import { describe, it, expect } from 'vitest';
import { NdfsFileSystem } from '../src/ndfs-filesystem.js';
import { ObjectFile } from '../src/object-file.js';
import { UserEntry } from '../src/user-entry.js';
import { ObjectBlockLimitError } from '../src/errors.js';
import { computeFileNumber } from '../src/object-entry.js';
import { ImageTemplate } from '../src/types.js';
import {
  FILES_PER_OBJECT_BLOCK,
  MAX_OBJECT_BLOCKS,
  PAGES_PER_INDEX_BLOCK,
  PAGES_PER_OBJECT_BLOCK,
  USERS_PER_INDEX_BLOCK,
} from '../src/constants.js';

function createFS(pages = 6000): NdfsFileSystem {
  return NdfsFileSystem.createImage({
    template: ImageTemplate.Custom,
    customPages: pages,
    directoryName: 'BLOCKS',
  });
}

describe('ObjectBlocks - byte 47', () => {
  it('gives a fresh user one object block, as SINTRAN does', () => {
    const ue = new UserEntry();
    expect(ue.maxObjectBlocks).toBe(1);
    expect(ue.allocatedObjectBlocks).toBe(1);
  });

  it('stores 0x00 for a default user - the reason the byte looked unused', () => {
    const buf = new UserEntry().toBytes();
    expect(buf[47]).toBe(0x00);
  });

  it.each([
    // raw, max, allocated
    [0x00, 1, 1], // default user: 256 files
    [0x31, 4, 2], // BIGMAN on the verified pack: max 1024, 2 blocks in use
    [0xff, 16, 16], // the SINTRAN K ceiling: 4096 files
    [0x10, 2, 1], // granted a second block, not yet used
  ])('round-trips byte 47 = 0x%s', (raw, max, allocated) => {
    const buf = new Uint8Array(64);
    buf[0] = 0x81; // valid-user flag
    buf[47] = raw as number;

    const ue = UserEntry.fromBytes(buf, 0)!;
    expect(ue.maxObjectBlocks).toBe(max);
    expect(ue.allocatedObjectBlocks).toBe(allocated);
    expect(ue.toBytes()[47]).toBe(raw);
  });
});

describe('ObjectBlocks - placement', () => {
  it('puts block n for user U at pages n*512 + U*8', () => {
    // Measured on the live pack: user 8's second block is at pages 576-583.
    const physical = ObjectFile.physicalSlot(8, 1, 0);
    const page = Math.floor(physical / 32);
    expect(page).toBe(1 * PAGES_PER_INDEX_BLOCK + 8 * PAGES_PER_OBJECT_BLOCK);
    expect(page).toBe(576);
  });

  it('reproduces the F0500 = FILE 307 vector', () => {
    // F0500 is owned by user 8 and sits at physical position 18483; SINTRAN
    // reports it as FILE 307. The old model gave 51, owned by user 72.
    expect(computeFileNumber(18483, 8)).toBe(307);
  });

  it('collides a second block with a high users first block', () => {
    // User U's block n occupies exactly user (n*64 + U)'s FIRST block.
    expect(ObjectFile.physicalSlot(8, 1, 0)).toBe(
      ObjectFile.physicalSlot(1 * USERS_PER_INDEX_BLOCK + 8, 0, 0),
    );
  });
});

describe('ObjectBlocks - creating past 256 files', () => {
  it('allocates a second block for the 257th file', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];
    fs.giveObjectBlocks(user.userName, 3);
    expect(user.allocatedObjectBlocks).toBe(1);

    for (let i = 0; i < FILES_PER_OBJECT_BLOCK + 1; i++) {
      fs.writeFile(`${user.userName}/F${String(i).padStart(4, '0')}:DATA`, new Uint8Array([1]));
    }

    expect(user.allocatedObjectBlocks).toBe(2);
  });

  it('numbers files contiguously across the 256 boundary', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];
    fs.giveObjectBlocks(user.userName, 3);

    const count = FILES_PER_OBJECT_BLOCK + 5;
    for (let i = 0; i < count; i++) {
      fs.writeFile(`${user.userName}/F${String(i).padStart(4, '0')}:DATA`, new Uint8Array([1]));
    }

    const reloaded = new NdfsFileSystem(fs.toBuffer(), true);
    const ru = reloaded.getUsers()[0];
    expect(ru.maxObjectBlocks).toBe(4);
    expect(ru.allocatedObjectBlocks).toBe(2);

    const numbers = reloaded
      .getObjectEntries()
      .filter((e) => e.userIndex === ru.userIndex)
      .map((e) => e.fileNumber)
      .sort((a, b) => a - b);

    expect(numbers).toEqual(Array.from({ length: count }, (_, i) => i));
  });

  it('fails only at the users real ceiling', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];
    expect(user.maxObjectBlocks).toBe(1);

    for (let i = 0; i < FILES_PER_OBJECT_BLOCK; i++) {
      fs.writeFile(`${user.userName}/F${String(i).padStart(4, '0')}:DATA`, new Uint8Array([1]));
    }

    expect(() =>
      fs.writeFile(`${user.userName}/OVERFLOW:DATA`, new Uint8Array([1])),
    ).toThrow(ObjectBlockLimitError);
  });
});

describe('ObjectBlocks - giveObjectBlocks', () => {
  it('adds to the maximum rather than setting a total', () => {
    // Matches the live measurement: BIGMAN was a default user reporting MAXIMUM
    // NUMBER OF FILES 256, and @GIVE-OBJECT-BLOCKS BIGMAN,3 took it to 1024.
    const fs = createFS();
    const user = fs.getUsers()[0];

    expect(fs.giveObjectBlocks(user.userName, 3)).toBe(4);
    expect(user.maxObjectBlocks).toBe(4);
    expect(fs.getObjectBlockInfo(user.userName).maxFiles).toBe(1024);
  });

  it('does not allocate the blocks it grants', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];

    fs.giveObjectBlocks(user.userName, 5);

    expect(user.maxObjectBlocks).toBe(6);
    expect(user.allocatedObjectBlocks).toBe(1);
  });

  it('defaults to a single block', () => {
    const fs = createFS();
    expect(fs.giveObjectBlocks(fs.getUsers()[0].userName)).toBe(2);
  });

  it('persists through a reload', () => {
    const fs = createFS();
    fs.giveObjectBlocks(fs.getUsers()[0].userName, 3);

    const reloaded = new NdfsFileSystem(fs.toBuffer(), true);
    expect(reloaded.getUsers()[0].maxObjectBlocks).toBe(4);
  });

  it('refuses to pass sixteen blocks', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];
    fs.giveObjectBlocks(user.userName, 15);
    expect(user.maxObjectBlocks).toBe(MAX_OBJECT_BLOCKS);

    expect(() => fs.giveObjectBlocks(user.userName, 1)).toThrow(/at most/);
  });

  it('rejects a grant that would overshoot rather than clamping it', () => {
    // Clamping would leave the caller believing it got what it asked for.
    const fs = createFS();
    const user = fs.getUsers()[0];

    expect(() => fs.giveObjectBlocks(user.userName, 20)).toThrow(/At most 15 more/);
    expect(user.maxObjectBlocks).toBe(1);
  });

  it('validates its arguments', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];

    expect(() => fs.giveObjectBlocks(user.userName, 0)).toThrow();
    expect(() => fs.giveObjectBlocks('NO-SUCH-USER', 1)).toThrow(/not found/);
  });
});

describe('ObjectBlocks - takeObjectBlocks', () => {
  it('lowers the maximum', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];
    fs.giveObjectBlocks(user.userName, 3);

    expect(fs.takeObjectBlocks(user.userName, 2)).toBe(2);
    expect(user.maxObjectBlocks).toBe(2);
  });

  it('refuses to orphan files', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];
    fs.giveObjectBlocks(user.userName, 3);
    for (let i = 0; i < FILES_PER_OBJECT_BLOCK + 1; i++) {
      fs.writeFile(`${user.userName}/F${String(i).padStart(4, '0')}:DATA`, new Uint8Array([1]));
    }
    expect(user.allocatedObjectBlocks).toBe(2);

    expect(() => fs.takeObjectBlocks(user.userName, 3)).toThrow(/orphan/);
    expect(user.maxObjectBlocks).toBe(4);
  });
});

describe('ObjectBlocks - the two limits', () => {
  it('names the remedy on a soft limit', () => {
    const err = new ObjectBlockLimitError('BIGMAN', 2, 2, false);

    expect(err.isHardLimit).toBe(false);
    expect(err.message).toContain('giveObjectBlocks');
    expect(err.message).toContain('GIVE-OBJECT-BLOCKS');
    expect(err.message).toContain('512 files'); // 2 blocks x 256
  });

  it('offers no remedy on a hard limit', () => {
    const err = new ObjectBlockLimitError('BIGMAN', MAX_OBJECT_BLOCKS, MAX_OBJECT_BLOCKS, true);

    expect(err.isHardLimit).toBe(true);
    expect(err.message).toContain('hard limit');
    expect(err.message).toContain('4096 files');
    // Must not point at a command that can only refuse.
    expect(err.message).not.toContain('giveObjectBlocks');
  });
});

describe('ObjectBlocks - getObjectBlockInfo', () => {
  it('reports the whole position', () => {
    const fs = createFS();
    const user = fs.getUsers()[0];
    fs.giveObjectBlocks(user.userName, 3);
    for (let i = 0; i < 10; i++) {
      fs.writeFile(`${user.userName}/F${String(i).padStart(4, '0')}:DATA`, new Uint8Array([1]));
    }

    const info = fs.getObjectBlockInfo(user.userName);

    expect(info.userName).toBe(user.userName);
    expect(info.maxObjectBlocks).toBe(4);
    expect(info.allocatedObjectBlocks).toBe(1);
    expect(info.filesInUse).toBe(10);
    expect(info.maxFiles).toBe(1024);
    expect(info.allocatedFiles).toBe(256);
    expect(info.canGrant).toBe(true);
    expect(info.grantableBlocks).toBe(12);
  });
});
