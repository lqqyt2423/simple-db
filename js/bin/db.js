#!/usr/bin/env node

'use strict';

const filename = process.argv[2];
if (!filename) {
  console.log('Must supply a database filename.');
  process.exit();
}

const readline = require('readline');
const { Table, Row, logInfo } = require('..');

async function main() {
  const table = await Table.open(filename);

  const rl = readline.createInterface({
    input: process.stdin,
  });

  process.stdout.write('db > ');
  for await (const line of rl) {
    if (line === '.exit') {
      rl.close();
      break;
    }

    if (line === '.info') {
      logInfo();
      process.stdout.write('db > ');
      continue;
    }

    if (line.startsWith('insert ')) {
      try {
        const row = Row.fromLine(line.replace('insert ', ''));
        await table.insert(row);
        console.log('Executed.')
      } catch (err) {
        console.log(err);
      }

      process.stdout.write('db > ');
      continue;
    }

    if (line === 'select') {
      try {
        await table.list();
        console.log('Executed.');
      } catch (err) {
        console.log(err);
      }

      process.stdout.write('db > ');
      continue;
    }

    console.log('Unrecognized command');
    process.stdout.write('db > ');
  }
  
  await table.close();
}

main().catch(console.error);
