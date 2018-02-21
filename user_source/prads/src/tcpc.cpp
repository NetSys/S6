/*
** Copyright (C) 2009 Redpill Linpro, AS.
** Copyright (C) 2009 Edward Fjellskål <edward.fjellskaal@redpill-linpro.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "dist.hh"
#include "stub.connection.hh"
#include "stub.asset.hh"

#include "prads.h"
#include "sys_func.h"
#include "servicefp.h"

extern bstring UNKNOWN;

void client_tcp4(packetinfo *pi, signature* sig_client_tcp)
{
    signature *tmpsig;
    bstring app, service_name;
    

#if 1
    int rc;                     /* PCRE */
    int ovector[15];
    int tmplen;
	
	if (pi->plen < PAYLOAD_MIN) return; // if almost no payload - skip
    /* should make a config.tcp_client_flowdept etc
     * a range between 500-1000 should be good!
     */
    if (pi->plen > 600) tmplen = 600;
        else tmplen = pi->plen;

    tmpsig = sig_client_tcp;

    
	while (tmpsig != NULL) {
        rc = pcre_exec(tmpsig->regex, tmpsig->study, (const char*)pi->payload, tmplen, 0, 0,
                       ovector, 15);
        if (rc != -1) {
            app = get_app_name(tmpsig, pi->payload, ovector, rc);
            printf("[*] - MATCH CLIENT IPv4/TCP: %s\n",(char *)bdata(app));
            ///update_asset_service(pi, tmpsig->service, app);
            pi->cxt->set_check(CXT_CLIENT_DONT_CHECK);
            bdestroy(app);
            return;
        }
        tmpsig = tmpsig->next;
    }
#endif
	
	/* From here FOR S6 TEST !!! RAndomly found signature */
	tmpsig = sig_client_tcp;
	int count = 0;
	while (tmpsig != NULL) {
		count++;
		tmpsig = tmpsig->next;
	}

	int idx = random() % (count * 2); /* detect asset with 50% chance */
	count = 0;
	tmpsig = sig_client_tcp;
	while (tmpsig != NULL) {
		if (count == idx) {
            app = get_app_name_from_sig(tmpsig);
            //printf("[*] - MATCH CLIENT IPv4/TCP: %s %s\n",
			//		(char *)bdata(tmpsig->service),
			//		(char *)bdata(app));
			if (pi->asset != nullptr)
				pi->asset->update_asset_service(pi->d_port, pi->proto, pi->sc, 
						tmpsig->idx, pi->pheader->ts.tv_sec);
			else
				fprintf(stderr, "No asset!\n");
            pi->cxt->set_check(CXT_CLIENT_DONT_CHECK);
            bdestroy(app);
			return;	
		}
		count++;
		tmpsig = tmpsig->next;
	}
	/* Up to here For S6 TEST !!! */
	
	// Should have a flag set to resolve unknowns to default service
    if ( !ISSET_SERVICE_UNKNOWN(pi->cxt->get_check())
        && (service_name = check_known_port(IP_PROTO_TCP,ntohs(pi->tcph->dst_port))) !=NULL ) {
        //printf("[*] - MATCH SERVICE IPv4/TCP Port: %s\n",(char *)bdata(service_name));
		if (pi->asset != nullptr)
			pi->asset->update_asset_service(pi->d_port, pi->proto, pi->sc, -1, 
					pi->pheader->ts.tv_sec);
		else
			fprintf(stderr, "No asset!\n");
        pi->cxt->set_check(CXT_SERVICE_UNKNOWN_SET);
        bdestroy(service_name);
    }
    
	pi->cxt->set_check(CXT_CLIENT_DONT_CHECK);
	pi->cxt->set_check(CXT_SERVICE_UNKNOWN_SET);
}

void client_tcp6(packetinfo *pi, signature* sig_client_tcp)
{
    int rc;                     /* PCRE */
    int ovector[15];
    signature *tmpsig;
    bstring app, service_name;

    if (pi->plen < 10) return; // if almost no payload - skip
    /* should make a config.tcp_client_flowdept etc
     * a range between 500-1000 should be good!
     */
    tmpsig = sig_client_tcp;
    while (tmpsig != NULL) {
        rc = pcre_exec(tmpsig->regex, tmpsig->study, (const char*) pi->payload, pi->plen, 0, 0,
                       ovector, 15);
        if (rc != -1) {
            app = get_app_name(tmpsig, pi->payload, ovector, rc);
            printf("[*] - MATCH CLIENT IPv6/TCP: %s\n",(char *)bdata(app));
			if (pi->asset != nullptr)
				pi->asset->update_asset_service(pi->d_port, pi->proto, pi->sc, 
						tmpsig->idx, pi->pheader->ts.tv_sec);
			else
				fprintf(stderr, "No asset!\n");
            pi->cxt->set_check(CXT_CLIENT_DONT_CHECK);
            bdestroy(app);
            return;
        }
        tmpsig = tmpsig->next;
    }
    if (!ISSET_CLIENT_UNKNOWN(pi->cxt->get_check())
        && (service_name = check_known_port(IP_PROTO_TCP,ntohs(pi->tcph->dst_port))) !=NULL ) {
        printf("[*] - MATCH CLIENT IPv4/TCP Port: %s\n",(char *)bdata(service_name));
		if (pi->asset != nullptr)
			pi->asset->update_asset_service(pi->d_port, pi->proto, pi->sc, -1, 
					pi->pheader->ts.tv_sec);
		else
			fprintf(stderr, "No asset!\n");
        pi->cxt->set_check(CXT_CLIENT_UNKNOWN_SET);
        bdestroy(service_name);
    }
}
