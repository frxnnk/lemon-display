import { serve } from "https://deno.land/std@0.177.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response(null, { status: 200, headers: corsHeaders });
  }

  try {
    const supabase = createClient(
      Deno.env.get("SUPABASE_URL")!,
      Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
    );

    const { device_id, firmware_ver } = await req.json();

    if (!device_id || typeof device_id !== "string") {
      return new Response(
        JSON.stringify({ error: "device_id is required" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // Build the update payload
    const updatePayload: Record<string, unknown> = {
      last_seen: new Date().toISOString(),
      is_online: true,
    };

    if (firmware_ver && typeof firmware_ver === "string") {
      updatePayload.firmware_ver = firmware_ver;
    }

    // Update the device
    const { error: updateError } = await supabase
      .from("devices")
      .update(updatePayload)
      .eq("id", device_id);

    if (updateError) {
      throw updateError;
    }

    // Fetch device + joined user info so the ESP32 can detect pairing status
    const { data: device, error: fetchError } = await supabase
      .from("devices")
      .select("user_id, users ( lemon_tag )")
      .eq("id", device_id)
      .maybeSingle();

    if (fetchError) {
      throw fetchError;
    }

    if (!device) {
      return new Response(
        JSON.stringify({ error: "Device not found" }),
        { status: 404, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // Extract lemon_tag from the joined users row
    const usersData = device.users as Record<string, unknown> | null;
    const lemonTag = usersData?.lemon_tag ?? null;

    return new Response(
      JSON.stringify({
        user_id: device.user_id ?? null,
        lemon_tag: lemonTag,
      }),
      { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } },
    );
  } catch (err) {
    const message = err instanceof Error ? err.message : "Unknown error";
    return new Response(
      JSON.stringify({ error: message }),
      { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } },
    );
  }
});
