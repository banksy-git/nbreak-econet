<script lang="ts">
  import type { ConnectionState } from "../../lib/stores";
  export let state: ConnectionState;
</script>

<div class="flex items-center gap-4">
  <div class="flex items-center gap-2 text-xs">
    <span
      class="inline-flex h-2 w-2 rounded-full"
      class:bg-emerald-500={state === "connected"}
      class:bg-amber-400={state === "connecting"}
      class:bg-red-500={state === "disconnected"}
    ></span>

    <span class="uppercase tracking-wide text-[0.65rem] text-gray-600">
      {#if state === "connected"}
        Online
      {:else if state === "connecting"}
        Connecting...
      {:else}
        Offline
      {/if}
    </span>
  </div>
</div>
