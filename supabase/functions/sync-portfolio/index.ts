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

    const { user_id, holdings } = await req.json();

    if (!user_id || typeof user_id !== "string") {
      return new Response(
        JSON.stringify({ error: "user_id is required" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    if (!Array.isArray(holdings)) {
      return new Response(
        JSON.stringify({ error: "holdings must be an array" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
      );
    }

    // Validate each holding
    for (const h of holdings) {
      if (!h.asset || typeof h.asset !== "string") {
        return new Response(
          JSON.stringify({ error: "Each holding must have a string 'asset'" }),
          { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
        );
      }
      if (typeof h.amount !== "number" || isNaN(h.amount)) {
        return new Response(
          JSON.stringify({ error: `Invalid amount for asset '${h.asset}'` }),
          { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } },
        );
      }
    }

    let upsertedCount = 0;

    for (const h of holdings) {
      const { error } = await supabase
        .from("portfolio_holdings")
        .upsert(
          {
            user_id,
            asset: h.asset,
            amount: h.amount,
            updated_at: new Date().toISOString(),
          },
          { onConflict: "user_id,asset" },
        );

      if (error) {
        throw error;
      }

      upsertedCount++;
    }

    return new Response(
      JSON.stringify({ success: true, count: upsertedCount }),
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
