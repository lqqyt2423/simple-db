'use strict';

/**
 * page structure
 * 
 * common header
 * node_type        1
 * is_root          1
 * parent_pointer   4
 * 
 * leaf page header
 * common_header    6
 * cell_len         4
 * next_leaf        4
 * 
 * leaf cell row
 * id               4
 * username         32
 * email            256
 * 
 * leaf page
 * leaf_header      14
 * cell_key_1       4
 * cell_value_1     292
 * ...
 * 
 * internal page header
 * common_header    6
 * children_len-1   4
 * right_child      4
 * 
 * internal page
 * internal_header  14
 * child_1_pointer  4
 * child_key        4
 */

const fs = require('fs');
const fsPromises = require('fs/promises');

const Page_Size = 4096;
const Max_Pages = 100;
const Page_Type = { leaf: 1, internal: 2 };

const Key_Size = 4;
const Id_Size = 4;
const Username_Size = 32;
const Email_Size = 256;
const Row_Size = Id_Size + Username_Size + Email_Size;

const Leaf_Cell_Size = Key_Size + Row_Size;
const Leaf_Page_Header_Size = 14;
const Leaf_Max_Cells = Math.floor((Page_Size - Leaf_Page_Header_Size) / Leaf_Cell_Size);

const Leaf_Split_Left_Len = Math.floor(Leaf_Max_Cells / 2);
const Leaf_Split_Right_Len = Leaf_Max_Cells - Leaf_Split_Left_Len;

function logInfo() {
  console.log(`
Page_Size:        ${Page_Size}
Max_Pages:        ${Max_Pages}
Key_Size:         ${Key_Size}
Row_Size:         ${Row_Size}
Leaf_Cell_Size:   ${Leaf_Cell_Size}
Leaf_Max_Cells:   ${Leaf_Max_Cells}
`);
}

class Cursor {
  constructor({ page = null, index = -1 } = {}) {
    /**
     * @type Page
     */
    this.page = page;
    this.index = index;
  }
}

class Row {
  constructor({ id = -1, username = '', email = '' } = {}) {
    this.id = id;
    this.username = username;
    this.email = email;
  }

  /**
   * 
   * @param {string} line 
   */
  static fromLine(line) {
    let [id, username, email] = line.trim().split(/\s+/);
    id = parseInt(id);
    if (isNaN(id) || id < 0) throw new Error('invalid id');
    
    username = username || '';
    email = email || '';

    if (Buffer.byteLength(username) > Username_Size - 1) throw new Error('username too long');
    if (Buffer.byteLength(email) > Email_Size - 1) throw new Error('email too long');

    return new Row({ id, username, email });
  }

  /**
   * 
   * @param {Buffer} buf 
   */
  static parse(buf) {
    const id = buf.readUInt32LE(0);
    
    const usernameEnd = buf.indexOf(0, Id_Size);
    const username = buf.slice(Id_Size, usernameEnd).toString('utf8');

    const emailEnd = buf.indexOf(0, Id_Size + Username_Size);
    const email = buf.slice(Id_Size + Username_Size, emailEnd).toString('utf8');

    return new Row({ id, username, email });
  }

  stringify() {
    const buf = Buffer.alloc(Row_Size);
    buf.writeUInt32LE(this.id, 0);
    Buffer.from(this.username).copy(buf, Id_Size);
    Buffer.from(this.email).copy(buf, Id_Size + Username_Size);
    return buf;
  }
}

class Page {
  constructor({ buf = null, index = -1 } = {}) {
    /**
     * @type Buffer
     */
    this.buf = buf;
    this.index = index;
  }

  static fromBuf(buf, index) {
    return new Page({ buf, index });
  }

  get type() {
    return this.buf[0];
  }
  set type(val) {
    this.buf[0] = val;
  }

  get isRoot() {
    return this.buf[1];
  }
  set isRoot(val) {
    this.buf[1] = val;
  }

  get leafCellLen() {
    return this.buf.readUInt32LE(6);
  }
  set leafCellLen(val) {
    this.buf.writeUInt32LE(val, 6);
  }

  get leafNextLeafPageIndex() {
    return this.buf.readUInt32LE(10);
  }
  set leafNextLeafPageIndex(val) {
    this.buf.writeUInt32LE(val, 10);
  }

  leafGetKeyOffset(index) {
    return Leaf_Page_Header_Size + Leaf_Cell_Size * index;
  }

  leafGetValueOffset(index) {
    return this.leafGetKeyOffset(index) + Key_Size;
  }

  leafKey(index) {
    return this.buf.readUInt32LE(this.leafGetKeyOffset(index));
  }

  leafValue(index) {
    const offset = this.leafGetValueOffset(index);
    return Row.parse(this.buf.slice(offset, offset + Row_Size));
  }

  find(key) {
    if (this.type === Page_Type.leaf) {
      // Binary search
      let minIndex = 0;
      let maxIndex = this.leafCellLen;
      while (maxIndex > minIndex) {
        const index = Math.floor((minIndex + maxIndex) / 2);
        const keyAtIndex = this.leafKey(index);
        if (key === keyAtIndex) {
          return new Cursor({ page: this, index });
        }
        if (key < keyAtIndex) {
          maxIndex = index;
        } else {
          minIndex = index + 1;
        }
      }
      return new Cursor({ page: this, index: minIndex });
    }

    throw new Error('find not impl');
  }

  /**
   * 
   * @param {number} index 
   * @param {number} key 
   * @param {Row} value 
   */
  insert(index, key, value) {
    if (index < this.leafCellLen) {
      // movie cells to right
      this.buf.copy(this.buf, this.leafGetKeyOffset(index + 1), this.leafGetKeyOffset(index), this.leafGetKeyOffset(this.leafCellLen));
    }

    const keyOffset = this.leafGetKeyOffset(index);
    this.buf.writeUInt32LE(key, keyOffset);
    value.stringify().copy(this.buf, keyOffset + Key_Size);
    this.leafCellLen++;
  }
}

class Table {
  constructor({ filename = '', fh = null, fileLen = 0, pageLen = 0, pages = [], rootPageIndex = -1 } = {}) {
    this.filename = filename;
    /**
     * @type fs.promises.FileHandle
     */
    this.fh = fh;
    this.fileLen = fileLen;
    this.pageLen = pageLen;
    /**
     * @type Page[]
     */
    this.pages = pages;
    this.rootPageIndex = rootPageIndex;
  }

  static async open(filename) {
    const fh = await fsPromises.open(filename, fs.constants.O_RDWR | fs.constants.O_CREAT);
    const fstat = await fh.stat();
    const fileLen = fstat.size;
    
    if (fileLen % Page_Size !== 0) {
      throw new Error('Db file is not a whole number of pages. Corrupt file.');
    }

    const table = new Table({
      filename,
      fh,
      fileLen,
      pageLen: fileLen / Page_Size,
      pages: new Array(Max_Pages).fill(null),
      rootPageIndex: 0,
    });

    if (table.pageLen === 0) {
      // New database file. Initialize page 0 as leaf node.
      const page = await table.getPage(0);
      page.type = Page_Type.leaf;
      page.isRoot = 1;
      page.leafCellLen = 0;
      page.leafNextLeafPageIndex = 0;
    }

    return table;
  }

  async getPage(pageIndex) {
    if (pageIndex >= Max_Pages) {
      throw new Error(`Tried to fetch page number out of bounds. ${pageIndex} >= ${Max_Pages}`);
    }

    if (!this.pages[pageIndex]) {
      const buf = Buffer.allocUnsafeSlow(Page_Size).fill(0);
      await this.fh.read(buf, 0, Page_Size, pageIndex * Page_Size);
      this.pages[pageIndex] = Page.fromBuf(buf, pageIndex);
    }

    return this.pages[pageIndex];
  }

  async getRoot() {
    return this.getPage(this.rootPageIndex);
  }

  async close() {
    for (const page of this.pages.filter(Boolean)) {
      await this.fh.write(page.buf, 0, Page_Size, page.index * Page_Size);
    }
    await this.fh.close();
  }

  /**
   * 
   * @param {Row} row 
   */
  async insert(row) {
    const key = row.id;

    const cursor = await this.find(key);
    if (key === cursor.page.leafKey(cursor.index)) {
      throw new Error('duplicate key');
    }

    // split
    if (cursor.page.leafCellLen >= Leaf_Max_Cells) {
      if (cursor.page.isRoot) {
        throw new Error('not impl');
      } else {
        throw new Error('not impl');
      }
    }

    cursor.page.insert(cursor.index, key, row);
  }
  
  async find(key) {
    const rootPage = await this.getRoot();
    const cursor = rootPage.find(key);
    return cursor;
  }

  async list() {
    const cursor = await this.find(0);
    console.log('len:', cursor.page.leafCellLen);
    while (cursor.page.leafCellLen > cursor.index) {
      const row = cursor.page.leafValue(cursor.index);
      console.log(row);
      cursor.index++;
    }
  }

  async show(key) {}
}


module.exports = {
  Row,
  Table,
  logInfo,
};
