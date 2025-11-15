<script lang="ts">
    import { connectionState, device } from "../../lib/stores"
    import ConnectionStatus from "../ui/ConnectionStatus.svelte";

    export let openSidebar;
</script>

<!-- Top Navbar -->
<header class="flex items-center justify-between bg-white border-b px-4 h-14">
  <div class="flex items-center gap-2">
    <!-- Mobile: sidebar toggle -->
    <button
      class="md:hidden inline-flex items-center justify-center p-2 rounded-md border hover:bg-gray-100 focus:outline-none focus:ring-2 focus:ring-sky-500"
      aria-label="Open navigation"
      on:click={openSidebar}
    >
      <svg
        class="w-5 h-5"
        fill="none"
        stroke="currentColor"
        stroke-width="2"
        viewBox="0 0 24 24"
      >
        <path
          stroke-linecap="round"
          stroke-linejoin="round"
          d="M4 6h16M4 12h16M4 18h16"
        />
      </svg>
    </button>

    <div class="flex flex-col">
      <img src="/logo.svg" alt="N-Break logo" class="h-7 w-auto" />
      <span class="text-xs text-gray-500">{$device.model}</span>
    </div>
    
  </div>

  <ConnectionStatus state={$connectionState} />

</header>
