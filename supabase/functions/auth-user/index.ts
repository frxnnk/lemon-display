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

    const { wallet_address, lemon_tag, display_name, email } = await req.json();

    if (!wallet_address || typeof wallet_address !== "string") {
      return new Response(
        JSON.stringify({ error: "wallet_address is required" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // NOTE: In production, verify a SIWE (Sign-In With Ethereum) signature here.
    // For now, we trust the wallet_address directly.

    // Check if user exists
    const { data: existing, error: lookupError } = await supabase
      .from("users")
      .select("id")
      .eq("wallet_address", wallet_address)
      .maybeSingle();

    if (lookupError) {
      throw lookupError;
    }

    if (existing) {
      // Update optional fields if provided
      const updates: Record<string, unknown> = {};
      if (lemon_tag !== undefined) updates.lemon_tag = lemon_tag;
      if (display_name !== undefined) updates.display_name = display_name;
      if (email !== undefined) updates.email = email;

      if (Object.keys(updates).length > 0) {
        const { error: updateError } = await supabase
          .from("users")
          .update(updates)
          .eq("id", existing.id);

        if (updateError) {
          throw updateError;
        }
      }

      return new Response(
        JSON.stringify({ user_id: existing.id, created: false }),
        { status: 200, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // Create new user
    const insertPayload: Record<string, unknown> = { wallet_address };
    if (lemon_tag !== undefined) insertPayload.lemon_tag = lemon_tag;
    if (display_name !== undefined) insertPayload.display_name = display_name;
    if (email !== undefined) insertPayload.email = email;

    const { data: newUser, error: insertError } = await supabase
      .from("users")
      .insert(insertPayload)
      .select("id")
      .single();

    if (insertError) {
      throw insertError;
    }

    return new Response(
      JSON.stringify({ user_id: newUser.id, created: true }),
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
