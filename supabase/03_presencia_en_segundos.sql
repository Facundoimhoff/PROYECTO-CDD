-- =====================================================================
-- ClimControl · 3) Presencia en minutos y segundos, y datos al instante
--
-- Pegar en Supabase → SQL Editor → Run (después de 02_vinculacion.sql).
-- Se puede volver a correr sin perder datos.
--
-- 1) "Aula vacía después de" pasa de minutos enteros a segundos
--    (0 a 3600 = hasta 1 hora). Lo que estaba guardado se convierte solo.
-- 2) La ESP32 ahora reporta cada 2 segundos: se guarda una lectura nueva
--    apenas cambia la temperatura, la humedad, la presencia o un equipo
--    (y si no cambia nada, una cada 10 segundos, como antes).
-- =====================================================================

do $$
begin
  if not exists (select 1 from information_schema.columns
                  where table_schema = 'public' and table_name = 'configuracion' and column_name = 'segundos_sin_movimiento') then
    alter table public.configuracion add column segundos_sin_movimiento int not null default 300;
    if exists (select 1 from information_schema.columns
                where table_schema = 'public' and table_name = 'configuracion' and column_name = 'minutos_sin_movimiento') then
      update public.configuracion set segundos_sin_movimiento = minutos_sin_movimiento * 60;
    end if;
    alter table public.configuracion add constraint segundos_sin_movimiento_validos
      check (segundos_sin_movimiento between 0 and 3600);
  end if;
  alter table public.configuracion drop column if exists minutos_sin_movimiento;
end $$;

grant update (segundos_sin_movimiento) on public.configuracion to authenticated;

-- La ESP32 llama a esta función cada 2 segundos (y apenas cambia un equipo).
-- Guarda la lectura, registra los eventos y le devuelve la configuración.
create or replace function public.dispositivo_reportar(
  p_codigo text, p_clave text,
  p_temperatura numeric default null, p_humedad numeric default null,
  p_hay_personas boolean default false, p_ventilador boolean default false,
  p_aire boolean default false, p_luces boolean default false)
returns json
language plpgsql security definer set search_path = '' as $$
declare
  v_disp bigint; v_aula bigint; v_usuario uuid;
  v_hay_previa boolean;
  v_fecha timestamptz; v_vent boolean; v_aire boolean; v_luces boolean; v_gente boolean;
  v_temp numeric; v_hum numeric;
  v_cambio boolean;
  c record;
begin
  select d.id, d.aula_id, a.usuario_id into v_disp, v_aula, v_usuario
    from public.dispositivos d join public.aulas a on a.id = d.aula_id
   where d.codigo = upper(trim(coalesce(p_codigo, ''))) and d.clave_hash = private.hash_clave(p_clave);
  if v_disp is null then raise exception 'DISPOSITIVO_INVALIDO' using errcode = '28000'; end if;

  p_hay_personas := coalesce(p_hay_personas, false);
  p_ventilador := coalesce(p_ventilador, false);
  p_aire := coalesce(p_aire, false);
  p_luces := coalesce(p_luces, false);
  if p_temperatura < -40 or p_temperatura > 85 then p_temperatura := null; end if;   -- el sensor falló
  if p_humedad < 0 or p_humedad > 100 then p_humedad := null; end if;
  p_temperatura := round(p_temperatura, 1);
  p_humedad := round(p_humedad, 1);

  select l.fecha_hora, l.ventilador, l.aire, l.luces, l.hay_personas, l.temperatura, l.humedad
    into v_fecha, v_vent, v_aire, v_luces, v_gente, v_temp, v_hum
    from public.lecturas l where l.aula_id = v_aula order by l.fecha_hora desc limit 1;
  v_hay_previa := found;

  -- Eventos: uno por cada cosa que cambió desde la lectura anterior
  insert into public.eventos (aula_id, equipo, encendido)
  select v_aula, e.equipo, e.ahora
    from (values ('ventilador', p_ventilador, coalesce(v_vent, false)),
                 ('aire',       p_aire,       coalesce(v_aire, false)),
                 ('luces',      p_luces,      coalesce(v_luces, false)),
                 ('presencia',  p_hay_personas, coalesce(v_gente, false))) as e(equipo, ahora, antes)
   where e.ahora <> e.antes;
  v_cambio := found;

  -- Lectura nueva: si cambió algo (la humedad, de a 2 % para no guardar el ruido del sensor)
  -- o si pasaron 10 segundos desde la anterior
  if not v_hay_previa or v_cambio
     or p_temperatura is distinct from v_temp
     or (p_humedad is null) <> (v_hum is null) or abs(p_humedad - v_hum) >= 2
     or now() - v_fecha >= interval '10 seconds' then
    insert into public.lecturas (aula_id, temperatura, humedad, hay_personas, ventilador, aire, luces)
    values (v_aula, p_temperatura, p_humedad, p_hay_personas, p_ventilador, p_aire, p_luces);
  end if;

  update public.dispositivos set ultimo_contacto = now() where id = v_disp;

  select cf.temp_ventilador, cf.temp_aire, cf.segundos_sin_movimiento into c
    from public.configuracion cf where cf.usuario_id = v_usuario;
  return json_build_object(
    'temp_ventilador',         coalesce(c.temp_ventilador, 24),
    'temp_aire',               coalesce(c.temp_aire, 27),
    'segundos_sin_movimiento', coalesce(c.segundos_sin_movimiento, 300));
end $$;

revoke execute on function public.dispositivo_reportar(text, text, numeric, numeric, boolean, boolean, boolean, boolean) from public;
grant execute on function public.dispositivo_reportar(text, text, numeric, numeric, boolean, boolean, boolean, boolean) to anon, authenticated;
