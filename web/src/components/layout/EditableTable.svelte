<script context="module" lang="ts">
    export interface ColumnDef<T extends Record<string, any>, K extends keyof T = keyof T> {
      label: string;
      key: K;
      type?: "string" | "number" | "boolean";
    }
  </script>
  
  <script lang="ts" generics="T extends Record<string, any>">
  
    export let columns: ColumnDef<T>[] = [];
    export let rows: T[] = [];
    export let onChange: (rows: T[]) => void = () => {};
  
    function updateCell(index: number, key: keyof T, value: any) {
      const col = columns.find(c => c.key === key);
      const casted =
        col?.type === "number" ? Number(value) :
        col?.type === "boolean" ? value === "true" :
        value;
  
      rows = rows.map((row, i) =>
        i === index ? { ...row, [key]: casted } : row
      );
  
      onChange(rows);
    }
  
    function addRow() {
      const empty = {} as T;
      for (const col of columns) {
        (empty as any)[col.key] =
          col.type === "number" ? 0 :
          col.type === "boolean" ? false :
          "";
      }
      rows = [...rows, empty];
      onChange(rows);
    }
  
    function removeRow(idx: number) {
      rows = rows.filter((_, i) => i !== idx);
      onChange(rows);
    }
  </script>
  <!-- simplified input rendering -->
<table class="min-w-full divide-y divide-gray-200">
    <thead>
      <tr>
        {#each columns as col}
          <th class="px-3 py-2">{col.label}</th>
        {/each}
        <th></th>
      </tr>
    </thead>
  
    <tbody>
      {#each rows as row, i}
        <tr>
          {#each columns as col}
            <td class="px-3 py-2">
              <input
                type={col.type === "number" ? "number" : "text"}
                value={row[col.key]}
                on:input={(e) =>
                  updateCell(i, col.key, (e.target as HTMLInputElement).value)
                }
                class="border rounded px-2 py-1 w-full"
              />
            </td>
          {/each}
          <td>
            <button on:click={() => removeRow(i)}>Delete</button>
          </td>
        </tr>
      {/each}
    </tbody>
  </table>
  
  <button class="px-3 py-1.5 text-xs rounded-md bg-sky-600 text-white hover:bg-sky-700 disabled:opacity-50"

  on:click={addRow}>Add Row</button>