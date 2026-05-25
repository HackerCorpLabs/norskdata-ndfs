import { describe, it, expect } from 'vitest';
import { wildmatch } from '../src/wildmatch.js';

describe('wildmatch', () => {
  it('matches exact strings', () => {
    expect(wildmatch('STARTUP:MODE', 'STARTUP:MODE')).toBe(true);
    expect(wildmatch('STARTUP:MODE', 'STARTUP:SYMB')).toBe(false);
  });

  it('handles * suffix', () => {
    expect(wildmatch('*:MODE', 'STARTUP:MODE')).toBe(true);
    expect(wildmatch('*:MODE', ':MODE')).toBe(true); // * matches empty
    expect(wildmatch('*:MODE', 'X:SYMB')).toBe(false);
  });

  it('handles * prefix and mid', () => {
    expect(wildmatch('STARTUP*', 'STARTUP:MODE')).toBe(true);
    expect(wildmatch('ST*MODE', 'STARTUP:MODE')).toBe(true);
    expect(wildmatch('ST*X', 'STARTUP:MODE')).toBe(false);
  });

  it('handles lone and consecutive stars', () => {
    expect(wildmatch('*', 'ANYTHING')).toBe(true);
    expect(wildmatch('*', '')).toBe(true);
    expect(wildmatch('**', 'abc')).toBe(true);
  });

  it('handles ? single char', () => {
    expect(wildmatch('FILE?:C', 'FILE1:C')).toBe(true);
    expect(wildmatch('FILE?:C', 'FILE:C')).toBe(false);
    expect(wildmatch('???', 'AB')).toBe(false);
  });

  it('supports case-insensitive matching', () => {
    expect(wildmatch('*:mode', 'STARTUP:MODE', true)).toBe(true);
    expect(wildmatch('*:mode', 'STARTUP:MODE', false)).toBe(false);
  });

  it('handles empty pattern', () => {
    expect(wildmatch('', '')).toBe(true);
    expect(wildmatch('', 'x')).toBe(false);
  });

  it('backtracks correctly', () => {
    expect(wildmatch('*a*b*c*', 'xaybzc')).toBe(true);
    expect(wildmatch('a*a*a', 'aaaa')).toBe(true);
    expect(wildmatch('a*b', 'aXXXc')).toBe(false);
  });
});
