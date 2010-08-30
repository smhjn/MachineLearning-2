/** 
 @cond
 #########################################################################
 # GPL License                                                           #
 #                                                                       #
 # This file is part of the Machine Learning Framework.                  #
 # Copyright (c) 2010, Philipp Kraus, <philipp.kraus@flashpixx.de>       #
 # This program is free software: you can redistribute it and/or modify  #
 # it under the terms of the GNU General Public License as published by  #
 # the Free Software Foundation, either version 3 of the License, or     #
 # (at your option) any later version.                                   #
 #                                                                       #
 # This program is distributed in the hope that it will be useful,       #
 # but WITHOUT ANY WARRANTY; without even the implied warranty of        #
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
 # GNU General Public License for more details.                          #
 #                                                                       #
 # You should have received a copy of the GNU General Public License     #
 # along with this program.  If not, see <http://www.gnu.org/licenses/>. #
 #########################################################################
 @endcond
 **/



#ifndef MACHINELEARNING_DISTANCES_NCD_HPP
#define MACHINELEARNING_DISTANCES_NCD_HPP

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/symmetric.hpp>

#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/bzip2.hpp>

#include <boost/iostreams/device/null.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/counter.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/ref.hpp>

#include <boost/thread.hpp>


#include "../exception/exception.h"



namespace machinelearning { namespace distances {   
    
    namespace ublas = boost::numeric::ublas;
    namespace bio   = boost::iostreams;
    
    
	/**
     * class for calculating the normalized compression distance (NCD)
     * with some different algorithms like gzip and bzip2
     * @todo set to unicode
     * @todo multithread, wavefront begin by (1,n), (1,n-1), (2,n) ...
     **/
    template<typename T> class ncd {
        
        public:
            
            enum compresstype
            {
                gzip    = 0, 
                bzip2   = 1
            };
        
            enum compresslevel
            {
                defaultcompression  = 0,
                bestspeed           = 1,
                bestcompression     = 2
            };
            
            
            ncd ();
            ncd ( const compresstype& );
            ublas::matrix<T> unsymmetric ( const std::vector<std::string>&, const bool& = false );
            ublas::symmetric_matrix<T, ublas::upper> symmetric ( const std::vector<std::string>&, const bool& = false );
            T calculate ( const std::string&, const std::string&, const bool& = false );
            void setCompressionLevel( const compresslevel& = defaultcompression );
            
        private:
            
            /** type for internal compression state **/
            const compresstype m_compress;
            /** parameter for gzip **/
            bio::gzip_params m_gzipparam;
            /** parameter for bzip2 **/
            bio::bzip2_params m_bzip2param;
        
        ublas::symmetric_matrix<T, ublas::upper> m_symmetric;
        ublas::matrix<T> m_unsymmetric;
        std::multimap<std::size_t, std::pair<std::size_t,std::size_t> > m_wavefront;    
        ublas::vector<std::size_t> m_cache;
        std::vector<std::string> m_sources;
        bool m_isfile;
        
            std::size_t deflate ( const std::string&, const std::string& = "" ) const;        
            void getWavefrontIndex( const std::size_t&, const std::size_t& );
            void multithread_deflate( const std::size_t&, const bool& );
        
    };
    

    
    
    /** default constructor
     * @overload
     **/
    template<typename T> inline ncd<T>::ncd() :
        m_compress ( gzip ),
        m_gzipparam( bio::gzip::default_compression ),
        m_bzip2param( 6 ),
        m_symmetric(),
        m_unsymmetric(),
        m_wavefront(),
        m_cache(),
        m_sources(),
        m_isfile( false )
    {}
    
    
    /** constructor with the compression parameter
     * @overload
     * @param p_compress enum value that is declared inside the class
     **/
    template<typename T> inline ncd<T>::ncd( const compresstype& p_compress ) :
        m_compress ( p_compress ),
        m_gzipparam( bio::gzip::default_compression ),
        m_bzip2param( 6 ),
        m_symmetric(),
        m_unsymmetric(),
        m_wavefront(),
        m_cache(),
        m_sources(),
        m_isfile( false )
    {}

    
    /** creates wavefront index pairs for using on multithreaded systems
     * @param p_size number of rows or columns
     * @param p_threads number of threads
     **/
    template<typename T> inline void ncd<T>::getWavefrontIndex( const std::size_t& p_size, const std::size_t& p_threads )
    {
        m_wavefront.clear();
        
        // create wavefront in this case: (0,n) = 0, (0,n-1) = 1, (1,n) = 2 ...
        // we must create only the upper or lower index position, the other one can be created of swapping the index position
        std::size_t n=0;
        for(std::size_t i=p_size-1; i > 0; --i)
            for(std::size_t j=0; (i+j) < p_size; ++j)
                m_wavefront.insert(  std::pair<std::size_t, std::pair<std::size_t,std::size_t> >(n++ % p_threads, std::pair<std::size_t,std::size_t>(j, i+j))  );
    }
    
    
    
    /** creates the thread and calculates the NCD symmetric or unsymmetric 
     * @param p_id thread id for reading pairs in wavefront
     * @param p_symmetric bool for creating the symmetric or unsymmetric data
     **/
    template<typename T> inline void ncd<T>::multithread_deflate( const std::size_t& p_id, const bool& p_symmetric )
    {
                
        if (p_symmetric)
            for(std::multimap<std::size_t, std::pair<std::size_t,std::size_t> >::iterator it=m_wavefront.lower_bound(p_id); it != m_wavefront.upper_bound(p_id); ++it) {
            
                // check for both index positions the cache and adds the data
                if (m_cache[it->second.first] == 0)
                    m_cache[it->second.first] = deflate(m_sources[it->second.first]);
            
                if (m_cache[it->second.second] == 0)
                    m_cache[it->second.second] = deflate(m_sources[it->second.second]);
            
                // calculate NCD and set max. to 1, because the defalter can create data greater than 1
                m_symmetric(it->second.first, it->second.second) = 
                    std::min( static_cast<T>(1),
                    static_cast<T>(deflate(m_sources[it->second.first], m_sources[it->second.second]) - std::min(m_cache[it->second.first], m_cache[it->second.second])) / std::max(m_cache[it->second.first], m_cache[it->second.second])
                ); 
            }
        
        else
            for(std::multimap<std::size_t, std::pair<std::size_t,std::size_t> >::iterator it=m_wavefront.lower_bound(p_id); it != m_wavefront.upper_bound(p_id); ++it) {
                
                // check for both index positions the cache and adds the data
                if (m_cache[it->second.first] == 0)
                    m_cache[it->second.first] = deflate(m_sources[it->second.first]);
                
                if (m_cache[it->second.second] == 0)
                    m_cache[it->second.second] = deflate(m_sources[it->second.second]);
                
                const std::size_t l_min = std::min(m_cache[it->second.first], m_cache[it->second.second]);
                const std::size_t l_max = std::max(m_cache[it->second.first], m_cache[it->second.second]);
                
                // calculate NCD and set max. to 1, because the defalter can create data greater than 1
                m_unsymmetric(it->second.first, it->second.second) = 
                    std::min( static_cast<T>(1),
                    static_cast<T>(deflate(m_sources[it->second.first], m_sources[it->second.second]) - l_min) / l_max
                ); 

                m_unsymmetric(it->second.second, it->second.first) = 
                    std::min( static_cast<T>(1),
                    static_cast<T>(deflate(m_sources[it->second.second], m_sources[it->second.first]) - l_min) / l_max
                ); 
            }
    }
    
    
    
	/** sets the compression level
	 * @param  p_level compression level
     **/
	template<typename T> inline void ncd<T>::setCompressionLevel( const compresslevel& p_level )
	{
        switch (p_level) 
        {
            case defaultcompression :   
                m_gzipparam     = bio::gzip_params( bio::gzip::default_compression );
                m_bzip2param    = bio::bzip2_params( 6 );
                break;
                
            case bestspeed          :   
                m_gzipparam = bio::gzip_params( bio::gzip::best_speed );
                m_bzip2param    = bio::bzip2_params( 1 );
                break;
            
            case bestcompression    :   
                m_gzipparam = bio::gzip_params( bio::gzip::best_compression );
                m_bzip2param    = bio::bzip2_params( 9 );
                break;
        }
	}
    
    
    
    /** calculate distances between two strings
     * @param p_str1 first string
     * @param p_str2 second string
     * @param p_isfile parameter for interpreting the string as a file with path
     * @return distance value
     **/   
    template<typename T> inline T ncd<T>::calculate( const std::string& p_str1, const std::string& p_str2, const bool& p_isfile )
    {
        m_isfile                   = p_isfile;
        
        const std::size_t l_first  = deflate(p_str1);
        const std::size_t l_second = deflate(p_str2);
        
        return ( static_cast<T>(deflate(p_str1, p_str2)) - std::min(l_first, l_second)) / std::max(l_first, l_second);
    }
    
    

    /** calculate all distances of the string vector (first item in the vector is
     * first row and colum in the returning matrix
     * @param p_strvec string vector
     * @param p_isfile parameter for interpreting the string as a file with path
     * @return dissimilarity matrix with std::vector x std::vector elements
    **/
    template<typename T> inline ublas::matrix<T> ncd<T>::unsymmetric( const std::vector<std::string>& p_strvec, const bool& p_isfile  )
    {
        if (p_strvec.size() == 0)
            throw exception::parameter(_("vector size must be greater than zero"));
        
        
        // create matrix, cache and compress
        m_isfile = p_isfile;
        
        ublas::matrix<T> l_distances( p_strvec.size(), p_strvec.size() );
        ublas::vector<std::size_t> l_cache( p_strvec.size() );
        
        // create content
        for (std::size_t i = 0; i < p_strvec.size(); ++i) {
            
            // create deflate to cache
            if (i==0)
                l_cache(i) = deflate(p_strvec[i]);
            
            // diagonal elements are always zero
            l_distances(i,i) = 0;
            
            for (std::size_t j = i+1; j < p_strvec.size(); ++j) {
                
                // create deflate to cache
                if (i==0)
                    l_cache(j) = deflate(p_strvec[j]);
                
                // determine min and max for NCD
                const std::size_t l_min    = std::min(l_cache(i), l_cache(j));
                const std::size_t l_max    = std::max(l_cache(i), l_cache(j));
                				
				// because of deflate algorithms there can be values greater than 1, so we check and set it to 1
                l_distances(i, j) = std::min(  ( static_cast<T>(deflate(p_strvec[i], p_strvec[j])) - l_min) / l_max,  static_cast<T>(1)  );    
                l_distances(j, i) = std::min(  ( static_cast<T>(deflate(p_strvec[j], p_strvec[i])) - l_min) / l_max,  static_cast<T>(1)  );  
            }

        }
        
        return l_distances;
    }
       
    
    /** calculate all distances of the string vector (first item in the vector is
     * first row and colum in the returning matrix
     * @param p_strvec string vector
     * @param p_isfile parameter for interpreting the string as a file with path
     * @return dissimilarity matrix with std::vector x std::vector elements
     **/
    template<typename T> inline ublas::symmetric_matrix<T, ublas::upper> ncd<T>::symmetric( const std::vector<std::string>& p_strvec, const bool& p_isfile  )
    {
        if (p_strvec.size() == 0)
            throw exception::parameter(_("vector size must be greater than zero"));
        
        // init data
        m_sources       = p_strvec;
        m_isfile        = p_isfile;
        m_cache         = ublas::vector<std::size_t>(p_strvec.size());
        m_symmetric     = ublas::symmetric_matrix<T, ublas::upper>( p_strvec.size(), p_strvec.size() );
        m_unsymmetric   = ublas::matrix<T>();
        
        
        // if we can use multithreads 
        if (boost::thread::hardware_concurrency() > 1) {
            
            // the first and last element are calculated in a single run, because
            // the first two thread are used the data directly
            m_cache(0) = deflate(p_strvec[0]);
            m_cache(p_strvec.size()-1) = deflate(p_strvec[p_strvec.size()-1]);
            
            // create wavefront index position
            getWavefrontIndex(p_strvec.size(), boost::thread::hardware_concurrency());
            
            // create threads
            boost::thread_group l_threadgroup;
            for(std::size_t i=0; i < boost::thread::hardware_concurrency(); ++i)
                l_threadgroup.create_thread(  boost::bind( &ncd<T>::multithread_deflate, this, i, true )  );
            
            // run threads and wait during all finished
            l_threadgroup.join_all();
        
            
        } else // otherwise (we can't use threads)
            for (std::size_t i = 0; i < m_sources.size(); ++i) {
                
                // create deflate to cache
                if (i==0)
                    m_cache(i) = deflate(m_sources[i]);
                
                m_symmetric(i,i) = 0;
                for (std::size_t j = i+1; j < m_sources.size(); ++j) {
                    if (i==0)
                        m_cache(j) = deflate(m_sources[j]);
                    
                    // because of deflate algorithms there can be values greater than 1, so we check and set it to 1
                    m_symmetric(i, j) = std::min(  static_cast<T>(1),
                                                   static_cast<T>(deflate(m_sources[i], m_sources[j]) - std::min(m_cache(i), m_cache(j))) / std::max(m_cache(i), m_cache(j))  );    
                }
                
            }
        
        return m_symmetric;
    }
    
    
    /** deflate a string or file with the algorithm
     * @param p_isfile parameter for interpreting the string as a file with path
     * @param p_str1 first string to compress
     * @param p_str2 optional second string to compress
     **/    
    template<typename T> inline std::size_t ncd<T>::deflate( const std::string& p_str1, const std::string& p_str2 ) const
    {
        if (p_str1.empty())
            throw exception::parameter(_("string size must be greater than zero"));
        
        
        // create output null stream, compressor and counter structure
        bio::counter l_counter;
        bio::basic_null_sink<char> l_null;
        bio::filtering_streambuf< bio::output > l_deflate;
        
        switch (m_compress) {
            case gzip   : l_deflate.push( bio::gzip_compressor( m_gzipparam ) );     break;
            case bzip2  : l_deflate.push( bio::bzip2_compressor( m_bzip2param ) );    break;
        }
        l_deflate.push( boost::ref(l_counter) );        
        l_deflate.push( l_null );
        
        
        // create stream for data and copy streamdata to output
        if (m_isfile) {
            // for filestreams copy data manually to deflate stream, cause boost::iostreams::copy close
            // source and destination stream after copy is finished, so we copy only data from first
            // stream into deflate and than from second stream
            
            std::ifstream l_file( p_str1.c_str(), std::ifstream::in | std::ifstream::binary );       
            if (!l_file.is_open())
                throw exception::parameter(_("file can not be opened"));
            
            std::copy( std::istream_iterator<char>(l_file), std::istream_iterator<char>(), std::ostreambuf_iterator<char>(&l_deflate) );
            l_file.close();
            
            if (!p_str2.empty()) {
                
                l_file.open( p_str2.c_str(), std::ifstream::in | std::ifstream::binary );
                if (!l_file.is_open())
                    throw exception::parameter(_("file can not be opened"));
                
                std::copy( std::istream_iterator<char>(l_file), std::istream_iterator<char>(), std::ostreambuf_iterator<char>(&l_deflate) );
                l_file.close();
                
            }
            
            // close deflate stream, cause only than counter returns correct value
            bio::close(l_deflate);
            
        } else {
            std::stringstream l_sstream( p_str1+p_str2 );
            bio::copy( l_sstream, l_deflate );
        }
        
        
        return l_counter.characters();
    }
    

};};
#endif
