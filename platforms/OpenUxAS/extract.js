// Use to parse https://www.ibm.com/docs/en/i/7.3?topic=extensions-standard-c-library-functions-table-by-name.
'use strict';

const table = document.getElementById('stalib__statable');

const superscriptRegex = /(.*)(<sup>[0-9]+<\/sup>)/;

const apiNames = Array.from(table.tBodies[0].children).map(
  (row) => {
    const match = row.children[0].innerHTML.match(superscriptRegex);
    if (match !== null) {
      return match[1];
    } else {
      return row.children[0].innerHTML;
    }
  },
);

for (const name of apiNames) {
  console.log(name);
}
