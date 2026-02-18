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

    // --- Rate limiting by IP ---
    const clientIp = req.headers.get("x-forwarded-for")?.split(",")[0]?.trim() || "unknown";

    const fiveMinutesAgo = new Date(Date.now() - 60 * 1000).toISOString();
    const { count: recentAttempts, error: rateError } = await supabase
      .from("pair_attempts")
      .select("*", { count: "exact", head: true })
      .eq("ip_address", clientIp)
      .gte("attempted_at", fiveMinutesAgo);

    if (rateError) {
      console.error("Rate limit check failed:", rateError.message);
      // Continue anyway; don't block pairing if the rate-limit table is missing
    } else if ((recentAttempts ?? 0) >= 5) {
      return new Response(
        JSON.stringify({ error: "Too many pairing attempts. Try again in a minute." }),
        { status: 429, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // Log this attempt
    await supabase
      .from("pair_attempts")
      .insert({ ip_address: clientIp, attempted_at: new Date().toISOString() })
      .then(() => {});

    // --- Parse body ---
    const { device_code, user_id } = await req.json();

    if (!device_code || typeof device_code !== "string") {
      return new Response(
        JSON.stringify({ error: "device_code is required" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    if (!user_id || typeof user_id !== "string") {
      return new Response(
        JSON.stringify({ error: "user_id is required" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // --- Look up device ---
    const { data: device, error: lookupError } = await supabase
      .from("devices")
      .select("id, user_id")
      .eq("device_code", device_code.toUpperCase())
      .maybeSingle();

    if (lookupError) {
      throw lookupError;
    }

    if (!device) {
      return new Response(
        JSON.stringify({ error: "Invalid device code" }),
        { status: 404, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    if (device.user_id) {
      return new Response(
        JSON.stringify({ error: "Device is already paired" }),
        { status: 409, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // --- Pair the device ---
    const { error: updateError } = await supabase
      .from("devices")
      .update({ user_id, paired_at: new Date().toISOString() })
      .eq("id", device.id);

    if (updateError) {
      throw updateError;
    }

    // --- Create default device_config ---
    const { error: configError } = await supabase
      .from("device_config")
      .insert({ device_id: device.id });

    if (configError) {
      // If config already exists, ignore the conflict
      if (!configError.message.includes("duplicate") && !configError.code?.includes("23505")) {
        throw configError;
      }
    }

    return new Response(
      JSON.stringify({ success: true, device_id: device.id }),
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
