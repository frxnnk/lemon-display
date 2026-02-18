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

    const { hardware_id, firmware_ver } = await req.json();

    if (!hardware_id || typeof hardware_id !== "string") {
      return new Response(
        JSON.stringify({ error: "hardware_id is required" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // Check if device already exists
    const { data: existing, error: lookupError } = await supabase
      .from("devices")
      .select("id, device_code")
      .eq("hardware_id", hardware_id)
      .maybeSingle();

    if (lookupError) {
      throw lookupError;
    }

    if (existing) {
      return new Response(
        JSON.stringify({ device_id: existing.id, device_code: existing.device_code }),
        { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // Generate a unique device code via SQL function
    const { data: codeResult, error: codeError } = await supabase
      .rpc("generate_device_code");

    if (codeError) {
      throw codeError;
    }

    const deviceCode = codeResult as string;

    // Insert new device
    const { data: newDevice, error: insertError } = await supabase
      .from("devices")
      .insert({
        hardware_id,
        device_code: deviceCode,
        firmware_ver: firmware_ver || null,
        is_online: true,
        last_seen: new Date().toISOString(),
      })
      .select("id, device_code")
      .single();

    if (insertError) {
      throw insertError;
    }

    return new Response(
      JSON.stringify({ device_id: newDevice.id, device_code: newDevice.device_code }),
      { status: 201, headers: { ...corsHeaders, "Content-Type": "application/json" } },
    );
  } catch (err) {
    const message = err instanceof Error ? err.message : "Unknown error";
    return new Response(
      JSON.stringify({ error: message }),
      { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } },
    );
  }
});
