<script lang="ts">
  import { onMount } from "svelte";
  import { activePage } from "../../lib/stores";

  import StatusPage from "../pages/StatusPage.svelte";
  import WifiPage from "../pages/WifiPage.svelte";
  import WifiApPage from "../pages/WifiApPage.svelte";
  import EconetPage from "../pages/EconetPage.svelte";
  import EconetClockPage from "../pages/EconetClockPage.svelte";
  import SystemPage from "../pages/SystemPage.svelte";
  import LogsPage from "../pages/LogsPage.svelte";
  import EconetUplinks from "../pages/EconetUplinks.svelte";

  export let mobileSidebarOpen = false;
  export let closeSidebar;

  const navItems = [
    { id: "status", label: "Status", component: StatusPage, icon: "gridicons--stats-up"},
    { id: "econet", label: "Econet and AUN", component: EconetPage,  icon: "lucide--network" },
    { id: "eclock", label: "Econet Clock", component: EconetClockPage,  icon: "meteor-icons--wave-square" },
    { id: "euplink", label: "Econet Uplinks", component: EconetUplinks,  icon: "mdi--satellite-uplink" },
    { id: "wifi", label: "WiFi network", component: WifiPage, icon: "mdi--wifi" },
    { id: "wifi_ap", label: "WiFi access point", component: WifiApPage, icon: "mdi--access-point" },
    { id: "system", label: "System", component: SystemPage, icon: "hugeicons--gears"},
    { id: "logs", label: "Logs", component: LogsPage, icon: "mdi--console"},
  ];

  onMount(() => {
    $activePage = StatusPage;
  });
</script>

<div class="flex overflow-hidden">
  <!-- Sidebar (desktop) -->
  <aside class="hidden md:flex md:flex-col w-56 bg-white border-r">
    <div class="px-4 py-3 border-b">
      <p
        class="text-[0.65rem] font-semibold text-gray-500 uppercase tracking-wide"
      >
        Menu
      </p>
    </div>

    <nav class="flex-1 px-2 py-3 space-y-1 text-sm">
      {#each navItems as item}
        <button
          class="w-full flex items-center justify-between px-2 py-2 rounded-md text-left"
          class:bg-sky-50={$activePage === item.component}
          class:text-sky-700={$activePage === item.component}
          class:text-gray-700={$activePage !== item.component}
          class:hover:bg-gray-100={$activePage !== item.component}
          on:click={() => ($activePage = item.component)}
        >
          <span class="flex items-center gap-2">
            <span class="icon-[{item.icon}] text-lg"></span>
            {item.label}
          </span>
          {#if $activePage === item.component}
            <span class="text-[0.65rem] uppercase tracking-wide text-sky-500"
              >●</span
            >
          {/if}
        </button>
      {/each}
    </nav>
  </aside>

  <!-- Sidebar (mobile) -->
  {#if mobileSidebarOpen}
    <div class="fixed inset-0 z-40 flex md:hidden">
      <button
        class="fixed inset-0 bg-black bg-opacity-40"
        on:click={closeSidebar}
        aria-label="close sidebar"
      ></button>

      <div
        class="relative flex flex-col w-56 max-w-full bg-white border-r shadow-xl"
      >
        <div class="flex items-center justify-between px-4 py-3 border-b">
          <span class="font-semibold text-sm">Menu</span>
          <button
            class="inline-flex items-center justify-center p-2 rounded-md hover:bg-gray-100 focus:outline-none focus:ring-2 focus:ring-sky-500"
            aria-label="Close navigation"
            on:click={closeSidebar}
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
                d="M6 18L18 6M6 6l12 12"
              />
            </svg>
          </button>
        </div>

        <nav class="flex-1 px-2 py-3 space-y-1 text-sm overflow-y-auto">
          {#each navItems as item}
            <button
              class="w-full flex items-center justify-between px-2 py-2 rounded-md text-left"
              class:bg-sky-50={$activePage === item.component}
              class:text-sky-700={$activePage === item.component}
              class:text-gray-700={$activePage !== item.component}
              class:hover:bg-gray-100={$activePage !== item.component}
              on:click={() => {
                $activePage = item.component;
                closeSidebar();
              }}
            >
              <span>{item.label}</span>
              {#if $activePage === item.component}
                <span
                  class="text-[0.65rem] uppercase tracking-wide text-sky-500"
                  >●</span
                >
              {/if}
            </button>
          {/each}
        </nav>
      </div>
    </div>
  {/if}
</div>
